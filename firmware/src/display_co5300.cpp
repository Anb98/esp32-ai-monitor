#include "display_co5300.h"
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "CO5300";

DisplayCO5300 display;

static spi_device_handle_t spi_dev = NULL;

DisplayCO5300::DisplayCO5300() : _sleeping(false), _buf1(nullptr), _buf2(nullptr) {}

void DisplayCO5300::initBus() {
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = LCD_QSPI_SCK;
    buscfg.data0_io_num = LCD_QSPI_D0;
    buscfg.data1_io_num = LCD_QSPI_D1;
    buscfg.data2_io_num = LCD_QSPI_D2;
    buscfg.data3_io_num = LCD_QSPI_D3;
    buscfg.max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t);
    buscfg.flags = SPICOMMON_BUSFLAG_QUAD;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits = 8;
    devcfg.address_bits = 24;
    devcfg.mode = 0;
    devcfg.clock_speed_hz = 40 * 1000 * 1000;
    devcfg.spics_io_num = LCD_QSPI_CS;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX;
    devcfg.queue_size = 10;

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
    }
}

// QSPI framing for the CO5300, matching Arduino_ESP32QSPI in the vendor SDK.
// The real opcode travels in the address field, so both the 0x02 prefix and
// the address must go out on all four lines: dropping MULTILINE_ADDR sends the
// opcode down a single line and the controller silently ignores the command.
void DisplayCO5300::writeCmd(uint32_t cmd) {
    if (!spi_dev) return;
    spi_transaction_t t = {};
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd = 0x02;
    t.addr = (cmd & 0xFF) << 8;
    t.length = 0;
    spi_device_polling_transmit(spi_dev, &t);
}

// Command parameters ride on a single data line; only the prefix and address
// are multiline. Pixel payloads are the exception and go through writePixels.
void DisplayCO5300::writeCmdData(uint32_t cmd, const uint8_t *data, size_t len) {
    if (!spi_dev) return;
    spi_transaction_t t = {};
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd = 0x02;
    t.addr = (cmd & 0xFF) << 8;
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(spi_dev, &t);
}

// Framebuffer writes use a different envelope than commands: prefix 0x32 and
// the fixed 0x3C (memory write continue) address, with the payload itself in
// quad mode. One call must stay within a single DMA transaction; the LVGL
// draw buffer is sized against that ceiling in init().
void DisplayCO5300::writePixels(const uint8_t *data, size_t len) {
    if (!spi_dev || len == 0) return;
    spi_transaction_t t = {};
    t.flags = SPI_TRANS_MODE_QIO;
    t.cmd = 0x32;
    t.addr = 0x003C00;
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(spi_dev, &t);
}

void DisplayCO5300::initPanel() {
    // Init sequence mirrors the vendor driver, Arduino_CO5300.cpp in
    // waveshareteam/ESP32-S3-Touch-AMOLED-2.16. Two of these are the reason a
    // hand-rolled sequence leaves the panel dark: 0xFE selects the command
    // page (without it later writes are dropped) and 0xC4 puts the controller
    // into the SPI mode that matches how we drive the QSPI bus.

    // Hardware reset: the vendor holds reset far longer than a typical LCD.
    gpio_set_direction((gpio_num_t)LCD_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    writeCmd(0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t page = 0x00;
    writeCmdData(0xFE, &page, 1); // Command page select

    uint8_t spiMode = 0x80;
    writeCmdData(0xC4, &spiMode, 1); // SPI mode control

    uint8_t colmod = 0x55; // 16 bit/pixel (RGB565)
    writeCmdData(0x3A, &colmod, 1);

    uint8_t ctrl = 0x20;
    writeCmdData(0x53, &ctrl, 1); // CTRL Display1

    uint8_t hbm = 0xFF;
    writeCmdData(0x63, &hbm, 1); // Brightness, HBM mode

    writeCmd(0x29); // Display ON

    uint8_t bright = 0xD0;
    writeCmdData(0x51, &bright, 1); // Brightness, normal mode

    uint8_t ce = 0x00;
    writeCmdData(0x58, &ce, 1); // Contrast enhancement off

    vTaskDelay(pdMS_TO_TICKS(50));
}

bool DisplayCO5300::init() {
    ESP_LOGI(TAG, "Initializing CO5300 AMOLED display (480x480)...");
    initBus();
    initPanel();

    // One flush must fit in a single SPI DMA transaction (32768 B on the
    // ESP32-S3), or the driver rejects it with "txdata transfer > hardware max
    // supported len" and the panel never receives the frame.
    // 480 px * 32 lines * 2 B = 30720 B, just under the ceiling.
    constexpr int kDrawBufLines = 32;
    static_assert(LCD_WIDTH * kDrawBufLines * sizeof(lv_color_t) <= 32768,
                  "draw buffer exceeds the SPI DMA transaction limit");

    // Draw buffers must be DMA-capable internal RAM, not PSRAM. The SPI DMA
    // reads these directly, and a PSRAM buffer is served through the cache:
    // the transfer can pick up stale bytes for lines the CPU just redrew,
    // which shows up as speckles and warped glyphs in the areas being updated.
    size_t bufSize = LCD_WIDTH * kDrawBufLines * sizeof(lv_color_t);
    _buf1 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    _buf2 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!_buf2) {
        ESP_LOGW(TAG, "Only one draw buffer available; rendering single-buffered");
    }

    if (!_buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer");
        return false;
    }

    lv_disp_draw_buf_init(&_drawBuf, _buf1, _buf2, LCD_WIDTH * kDrawBufLines);

    lv_disp_drv_init(&_dispDrv);
    _dispDrv.hor_res = LCD_WIDTH;
    _dispDrv.ver_res = LCD_HEIGHT;
    _dispDrv.flush_cb = flushCallback;
    _dispDrv.rounder_cb = rounderCallback;
    _dispDrv.draw_buf = &_drawBuf;
    _dispDrv.user_data = this;
    lv_disp_drv_register(&_dispDrv);
    setRotation(0);

    ESP_LOGI(TAG, "CO5300 display initialized successfully.");
    return true;
}

// The CO5300 addresses its framebuffer in pixel pairs. An area starting on an
// odd column, or one pixel wide, lands half a pixel off and every following
// line inherits the shift, which reads as sheared, italic-looking glyphs and
// stray dots. Snap each flush to an even span before LVGL renders it.
void DisplayCO5300::rounderCallback(lv_disp_drv_t *disp_drv, lv_area_t *area) {
    area->x1 &= ~1;
    area->x2 |= 1;
}

void DisplayCO5300::flushCallback(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    if (!spi_dev) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    uint8_t col_data[4] = {
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF),
        (uint8_t)(x2 >> 8), (uint8_t)(x2 & 0xFF)
    };
    uint8_t row_data[4] = {
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF),
        (uint8_t)(y2 >> 8), (uint8_t)(y2 & 0xFF)
    };

    // Set Column Address (0x2A)
    display.writeCmdData(0x2A, col_data, 4);
    // Set Row Address (0x2B)
    display.writeCmdData(0x2B, row_data, 4);

    // Open the memory write, then stream the pixels in quad mode.
    display.writeCmd(0x2C);
    size_t len = (x2 - x1 + 1) * (y2 - y1 + 1) * sizeof(lv_color_t);
    display.writePixels((const uint8_t *)color_p, len);

    lv_disp_flush_ready(disp_drv);
}

void DisplayCO5300::sleep() {
    if (_sleeping) return;
    ESP_LOGI(TAG, "Putting CO5300 display to sleep...");
    writeCmd(0x28); // Display OFF
    vTaskDelay(pdMS_TO_TICKS(20));
    writeCmd(0x10); // Sleep IN
    _sleeping = true;
}

void DisplayCO5300::wake() {
    if (!_sleeping) return;
    ESP_LOGI(TAG, "Waking up CO5300 display...");
    writeCmd(0x11); // Sleep OUT
    vTaskDelay(pdMS_TO_TICKS(120));
    writeCmd(0x29); // Display ON
    _sleeping = false;
}

// The panel is square, so orientation is purely a MADCTL scan-order change:
// no resolution swap, no software rotate, no per-flush CPU cost. LVGL's
// sw_rotate does the same job by rotating each buffer on the CPU, which both
// costs time and corrupts partial redraws when it lands mid-flush.
void DisplayCO5300::setRotation(uint8_t rotation) {
    // Measured on hardware: the panel is mounted a half turn from the
    // controller's default scan order, so upright is 0xC0, not 0x00.
    static const uint8_t kMadctl[4] = {
        0xC0, // 0 degrees (upright)
        0xA0, // 90
        0x00, // 180
        0x60, // 270
    };
    uint8_t madctl = kMadctl[rotation & 0x03];
    writeCmdData(0x36, &madctl, 1);
    lv_obj_invalidate(lv_scr_act());
}

void DisplayCO5300::setBrightness(uint8_t brightness) {
    writeCmdData(0x51, &brightness, 1);
}
