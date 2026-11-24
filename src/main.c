/*
 * WS2812 / WS2812B ("one-wire" addressable RGB) strip driver for ESP32-C3.
 * Uses the RMT peripheral to generate the 800 kHz bit timing on GPIO2
 * and fills the whole strip with magenta.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#define LED_GPIO        2       // data pin of the strip
#define LED_COUNT       6      // <-- set to the number of LEDs on your strip
#define LED_BRIGHTNESS  80      // 0..255, keep it low-ish to save battery/current

#define SIMPLE_LED_GPIO 1       // base of the BJT (via 1k) driving the 5 plain LEDs

#define RMT_RES_HZ      10000000  // 10 MHz -> 1 tick = 0.1 us

static const char *TAG = "dress";

static uint8_t pixels[LED_COUNT * 3];   // GRB order, as WS2812 expects

static rmt_channel_handle_t tx_chan;
static rmt_encoder_handle_t encoder;

static void strip_init(void)
{
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&chan_cfg, &tx_chan));

    // WS2812 bit timings (datasheet: T0H 0.4, T0L 0.85, T1H 0.8, T1L 0.45 us, +-150 ns)
    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 }, // 0.3 / 0.9 us
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 }, // 0.9 / 0.3 us
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&enc_cfg, &encoder));
    ESP_ERROR_CHECK(rmt_enable(tx_chan));
}

static void simple_leds_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << SIMPLE_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

static void simple_leds_set(bool on)
{
    // NPN low-side switch: HIGH on the base = transistor on = LEDs lit
    gpio_set_level(SIMPLE_LED_GPIO, on ? 1 : 0);
}

static void strip_set_pixel(int i, uint8_t r, uint8_t g, uint8_t b)
{
    pixels[i * 3 + 0] = g;
    pixels[i * 3 + 1] = r;
    pixels[i * 3 + 2] = b;
}

static void strip_show(void)
{
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(tx_chan, encoder, pixels, sizeof(pixels), &tx_cfg));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx_chan, portMAX_DELAY));
    // line idles low; >280 us of low = latch/reset for WS2812B
    vTaskDelay(pdMS_TO_TICKS(1));
}

void app_main(void)
{
    strip_init();
    simple_leds_init();
    simple_leds_set(true);
    ESP_LOGI(TAG, "Plain LEDs ON (BJT on GPIO%d)", SIMPLE_LED_GPIO);

    // Magenta = full red + full blue, no green
    for (int i = 0; i < LED_COUNT; i++) {
        strip_set_pixel(i, LED_BRIGHTNESS, 0, LED_BRIGHTNESS);
    }

    ESP_LOGI(TAG, "Magenta on %d LEDs, GPIO%d", LED_COUNT, LED_GPIO);

    while (1) {
        strip_show();               // refresh periodically (harmless, survives glitches)
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
