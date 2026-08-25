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

void DisplayCO5300::writeCmd(uint32_t cmd) {
    if (!spi_dev) return;
    spi_transaction_t t = {};
    t.flags = SPI_TRANS_MULTILINE_CMD;
    t.cmd = 0x02; // Standard QSPI write command prefix
    t.addr = (cmd & 0xFF) << 8;
    t.length = 0;
    spi_device_polling_transmit(spi_dev, &t);
}

void DisplayCO5300::writeCmdData(uint32_t cmd, const uint8_t *data, size_t len) {
    if (!spi_dev) return;
    spi_transaction_t t = {};
    t.flags = SPI_TRANS_MODE_QIO;
    t.cmd = 0x02;
    t.addr = (cmd & 0xFF) << 8;
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(spi_dev, &t);
}

void DisplayCO5300::initPanel() {
    // Hardware reset
    gpio_set_direction((gpio_num_t)LCD_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // CO5300 Init Sequence
    writeCmd(0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t madctl = 0x00; // Orientation
    writeCmdData(0x36, &madctl, 1);

    uint8_t colmod = 0x55; // 16-bit/pixel (RGB565)
    writeCmdData(0x3A, &colmod, 1);

    uint8_t bright = 0xFF; // Max Brightness
    writeCmdData(0x51, &bright, 1);

    uint8_t ctrl = 0x2C;
    writeCmdData(0x53, &ctrl, 1);

    writeCmd(0x29); // Display ON
    vTaskDelay(pdMS_TO_TICKS(50));
}

bool DisplayCO5300::init() {
    ESP_LOGI(TAG, "Initializing CO5300 AMOLED display (480x480)...");
    initBus();
    initPanel();

    // Allocate LVGL draw buffers in PSRAM
    size_t bufSize = LCD_WIDTH * 40 * sizeof(lv_color_t);
    _buf1 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_buf1) {
        ESP_LOGW(TAG, "Failed to allocate draw buf 1 in PSRAM, falling back to internal RAM");
        _buf1 = (lv_color_t *)malloc(bufSize);
    }
    _buf2 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_buf2) {
        ESP_LOGW(TAG, "Failed to allocate draw buf 2 in PSRAM, falling back to internal RAM");
        _buf2 = (lv_color_t *)malloc(bufSize);
    }

    if (!_buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer");
        return false;
    }

    lv_disp_draw_buf_init(&_drawBuf, _buf1, _buf2, LCD_WIDTH * 40);

    lv_disp_drv_init(&_dispDrv);
    _dispDrv.hor_res = LCD_WIDTH;
    _dispDrv.ver_res = LCD_HEIGHT;
    _dispDrv.flush_cb = flushCallback;
    _dispDrv.draw_buf = &_drawBuf;
    _dispDrv.user_data = this;
    lv_disp_drv_register(&_dispDrv);

    ESP_LOGI(TAG, "CO5300 display initialized successfully.");
    return true;
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

    // Memory Write (0x2C)
    size_t len = (x2 - x1 + 1) * (y2 - y1 + 1) * sizeof(lv_color_t);
    display.writeCmdData(0x2C, (const uint8_t *)color_p, len);

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

void DisplayCO5300::setBrightness(uint8_t brightness) {
    writeCmdData(0x51, &brightness, 1);
}
