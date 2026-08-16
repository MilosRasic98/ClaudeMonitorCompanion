#include "display.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "pins.h"
#include "tuning.h"

namespace display {
namespace {

esp_lcd_panel_handle_t s_panel = nullptr;
esp_lcd_panel_io_handle_t s_io = nullptr;

// Strip buffers live in internal DMA-capable SRAM, sized for the widest span we
// ever push: full panel width by one grid row. PSRAM is deliberately kept out
// of the render path — DMA from PSRAM breaks when the flash cache is disabled,
// and PSRAM writes are several times slower than SRAM.
const size_t kStripPx = LCD_W * GRID_Q;

// esp_lcd_panel_draw_bitmap() is asynchronous: it queues a DMA transfer and
// returns. Refilling a shared strip buffer before that transfer has read it out
// corrupts pixels still in flight — visibly so when an erase and a draw in
// different colours are issued back to back, which is every frame the eyes move
// sideways.
//
// The fix exploits the fact that every shape here is a solid colour: keep one
// pre-filled buffer per colour instead of one shared buffer that gets rewritten.
// Nothing ever mutates a buffer that might be in flight, so transfers pipeline
// freely and no frame has to wait on the one before it. Two slots is exactly
// enough — the face only ever uses the current orange and black.
struct ColorBuf {
  uint16_t *px = nullptr;
  uint16_t color = 0;
  bool valid = false;
};
ColorBuf s_bufs[2];
uint8_t s_next_slot = 0;

// Only consulted when a buffer's colour has to change, which happens during
// colour calibration and nowhere else.
volatile uint32_t s_sent = 0;
volatile uint32_t s_done = 0;

bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t,
                                   esp_lcd_panel_io_event_data_t *, void *) {
  s_done++;
  return false;
}

void drain() {
  const uint32_t deadline = millis() + 100;
  while (s_done != s_sent && millis() < deadline) taskYIELD();
}

uint16_t *buffer_for(uint16_t color) {
  for (ColorBuf &b : s_bufs) {
    if (b.valid && b.color == color) return b.px;
  }
  ColorBuf &b = s_bufs[s_next_slot];
  s_next_slot = (uint8_t)((s_next_slot + 1) % 2);

  drain();  // never rewrite a buffer the DMA might still be reading
  for (size_t i = 0; i < kStripPx; i++) b.px[i] = color;
  b.color = color;
  b.valid = true;
  return b.px;
}

const ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
const ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;

void backlight_init() {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_8_BIT;
  t.timer_num = kBacklightTimer;
  t.freq_hz = 5000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&t));

  ledc_channel_config_t c = {};
  c.gpio_num = PIN_LCD_BL;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = kBacklightChannel;
  c.timer_sel = kBacklightTimer;
  c.duty = 0;
  c.hpoint = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&c));
}

}  // namespace

bool begin() {
  for (ColorBuf &b : s_bufs) {
    b.px = (uint16_t *)heap_caps_malloc(kStripPx * sizeof(uint16_t),
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b.px) return false;
  }

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_LCD_MOSI;
  bus.miso_io_num = -1;  // write-only panel, no TE pin either
  bus.sclk_io_num = PIN_LCD_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = (int)(kStripPx * sizeof(uint16_t));
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = PIN_LCD_CS;
  io.dc_gpio_num = PIN_LCD_DC;
  io.spi_mode = 0;
  io.pclk_hz = LCD_SPI_HZ;
  io.trans_queue_depth = 10;
  io.lcd_cmd_bits = 8;
  io.lcd_param_bits = 8;
  io.on_color_trans_done = on_color_trans_done;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &s_io));

  esp_lcd_panel_dev_config_t dev = {};
  dev.reset_gpio_num = PIN_LCD_RST;
  dev.bits_per_pixel = 16;
  // The colour-order field was renamed in IDF 5.0.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
#else
  dev.color_space = ESP_LCD_COLOR_SPACE_RGB;
#endif
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &dev, &s_panel));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

  // Mandatory on this IPS panel. Without it the orange field renders cyan.
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));

  // 240x280 panel inside 240x320 of controller RAM.
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, LCD_GAP_Y));

  // esp_lcd_panel_disp_on_off() arrived in IDF 5.0; before that it was the
  // inverted esp_lcd_panel_disp_off().
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
#else
  ESP_ERROR_CHECK(esp_lcd_panel_disp_off(s_panel, false));
#endif

  backlight_init();
  return true;
}

void IRAM_ATTR fill_rect(int x, int y, int w, int h, uint16_t color) {
  // Clip rather than trusting callers — animation maths overshoots by design.
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;

  const size_t row_px = (size_t)w;
  size_t rows_per_push = kStripPx / row_px;
  if (rows_per_push == 0) rows_per_push = 1;

  uint16_t *buf = buffer_for(color);

  int y0 = y;
  while (y0 < y + h) {
    int chunk = (int)rows_per_push;
    if (y0 + chunk > y + h) chunk = y + h - y0;
    esp_lcd_panel_draw_bitmap(s_panel, x, y0, x + w, y0 + chunk, buf);
    s_sent++;
    y0 += chunk;
  }
}

void backlight(uint8_t duty) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
}

}  // namespace display
