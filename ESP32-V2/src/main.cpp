#include <stdio.h>
#include <string.h>
#include "esp_sntp.h"
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "vernon_st7789t.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif
extern const uint8_t big_digits[11][40][3];
#ifdef __cplusplus
}
#endif

extern "C" const uint8_t* get_char_bitmap(char c);

static const char *TAG = "MAIN";
static esp_lcd_panel_handle_t panel_handle = NULL;
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// ===== LED (RMT-based WS2812B) =====
static rmt_channel_handle_t led_rmt_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;

static void led_init(void) {
    rmt_tx_channel_config_t tx_chan_config;
    memset(&tx_chan_config, 0, sizeof(tx_chan_config));
    tx_chan_config.gpio_num = (gpio_num_t)8;
    tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_chan_config.mem_block_symbols = 64;
    tx_chan_config.resolution_hz = 10 * 1000 * 1000;
    tx_chan_config.trans_queue_depth = 1;
    rmt_new_tx_channel(&tx_chan_config, &led_rmt_chan);

    rmt_bytes_encoder_config_t encoder_cfg;
    memset(&encoder_cfg, 0, sizeof(encoder_cfg));
    encoder_cfg.bit0.duration0 = 3;
    encoder_cfg.bit0.level0 = 1;
    encoder_cfg.bit0.duration1 = 9;
    encoder_cfg.bit0.level1 = 0;
    encoder_cfg.bit1.duration0 = 7;
    encoder_cfg.bit1.level0 = 1;
    encoder_cfg.bit1.duration1 = 5;
    encoder_cfg.bit1.level1 = 0;
    rmt_new_bytes_encoder(&encoder_cfg, &led_encoder);
    rmt_enable(led_rmt_chan);
}

static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t grb[3] = {g, r, b};
    rmt_transmit_config_t tx_config;
    memset(&tx_config, 0, sizeof(tx_config));
    tx_config.loop_count = 0;
    rmt_transmit(led_rmt_chan, led_encoder, grb, 3, &tx_config);
}

static void led_off(void) { led_set(0, 0, 0); }

// ===== State tracking =====
static bool wifi_connected = false;
static bool pn532_ok = false;
static int64_t last_card_check_us = 0;
static int64_t card_debounce_until_us = 0;

// ===== Backlight =====
static void bk_init(void) {
    ledc_timer_config_t timer;
    memset(&timer, 0, sizeof(timer));
    timer.duty_resolution = LEDC_TIMER_13_BIT;
    timer.freq_hz = 5000;
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = LEDC_TIMER_0;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);
    ledc_channel_config_t ch;
    memset(&ch, 0, sizeof(ch));
    ch.channel = LEDC_CHANNEL_0;
    ch.duty = 0;
    ch.gpio_num = (gpio_num_t)DISPLAY_BL;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.timer_sel = LEDC_TIMER_0;
    ledc_channel_config(&ch);
}

static void bk_light(uint8_t pct) {
    if (pct > 100) pct = 100;
    uint32_t max_duty = (1 << 13) - 1;
    uint32_t duty = pct == 0 ? 0 : max_duty - (81 * (100 - pct));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ===== Display init (using ESP-IDF LCD panel API from official demo) =====
static void display_init(void) {
    ESP_LOGI(TAG, "LCD init start");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config;
    memset(&io_config, 0, sizeof(io_config));
    io_config.dc_gpio_num = DISPLAY_DC;
    io_config.cs_gpio_num = DISPLAY_CS;
    io_config.pclk_hz = 12 * 1000 * 1000;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    // SPI bus for PN532 on same host – will be added later

    esp_lcd_panel_dev_st7789t_config_t panel_config;
    memset(&panel_config, 0, sizeof(panel_config));
    panel_config.reset_gpio_num = DISPLAY_RST;
    panel_config.rgb_endian = LCD_RGB_ENDIAN_BGR;
    panel_config.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789t(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // Landscape 320x172: swap x/y, mirror both, gap on Y
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 34));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    bk_init();
    bk_light(DISPLAY_BRIGHTNESS);
    ESP_LOGI(TAG, "LCD init done (%d%%)", DISPLAY_BRIGHTNESS);
}

static void disp_fill_rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t r, uint16_t g, uint16_t b) {
    if (!panel_handle) return;
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    uint8_t *buf = (uint8_t *)malloc(w * h * 2);
    if (buf) {
        uint16_t color = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3);
        uint8_t hi = color>>8, lo = color&0xFF;
        for (int i = 0; i < w * h; i++) { buf[i*2] = hi; buf[i*2+1] = lo; }
        esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x1+w, y1+h, buf);
        free(buf);
    }
}

static void disp_clear_black(void) {
    if (!panel_handle) return;
    // Use raw panel IO to clear the FULL framebuffer (240x320), bypassing gap
    // The Vernon driver adds gap to draw_bitmap, so rows 0-33 are never written
    // We need to send CASET=0..239, RASET=0..319 directly
    esp_lcd_panel_io_handle_t io = NULL;
    // We can access the IO via the panel handle's internal structure
    // But we can't easily access it. Alternative: draw a rect that covers the gap
    // by drawing at negative coordinates (which get shifted by gap into the correct range)
    // Actually, set_gap first to 0, clear, then restore
    esp_lcd_panel_set_gap(panel_handle, 0, 0);
    int w = 320, h = 172;
    uint8_t *buf = (uint8_t*)calloc(w * h, 2);
    if (buf) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, w, h, buf);
        free(buf);
    }
    esp_lcd_panel_set_gap(panel_handle, 0, 34);
}

static void disp_clear_rgb(uint8_t r, uint8_t g, uint8_t b) {
    disp_fill_rect(0, 0, 319, 171, r, g, b);
}

// ===== Clock task =====
static void clock_task(void *arg) {
    int char_w = 24, char_h = 40, spacing = 4;
    int total_w = 8*char_w + 7*spacing;
    int start_x = 50, start_y = 66;
    extern const uint8_t big_digits[11][40][3];
    char last_time[10] = "";
    while(1) {
        time_t t; time(&t);
        struct tm tm; localtime_r(&t, &tm);
        char ts[9];
        snprintf(ts, sizeof(ts), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        if (strcmp(ts, last_time) != 0) {
            strcpy(last_time, ts);
            uint8_t *buf = (uint8_t*)calloc(total_w * char_h, 2);
            if (buf) {
                for (int ci = 0; ci < 8; ci++) {
                    char c = ts[ci];
                    int idx;
                    if (c >= '0' && c <= '9') idx = c - '0';
                    else if (c == ':') idx = 10;
                    else continue;
                    int xo = ci * (char_w + spacing);
                    for (int r = 0; r < char_h; r++) {
                        uint8_t d0 = big_digits[idx][r][0];
                        uint8_t d1 = big_digits[idx][r][1];
                        uint8_t d2 = big_digits[idx][r][2];
                        for (int ci2 = 0; ci2 < char_w; ci2++) {
                            int bi = ci2 / 8, bt = 7 - (ci2 % 8);
                            int on = 0;
                            if (bi == 0) on = (d0 >> bt) & 1;
                            else if (bi == 1) on = (d1 >> bt) & 1;
                            else on = (d2 >> bt) & 1;
                            if (on) {
                                int px = (r * total_w + (xo + ci2)) * 2;
                                buf[px] = 0xFF; buf[px+1] = 0xFF;
                            }
                        }
                    }
                }
                if (panel_handle) {
                    esp_lcd_panel_draw_bitmap(panel_handle, start_x, start_y, start_x + total_w, start_y + char_h, buf);
                }
                free(buf);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ===== PN532 (using garag/esp-idf-pn532 library, HSU/UART) =====
#include "pn532_driver.h"
#include "pn532_driver_hsu.h"
#include "pn532.h"

static pn532_io_t pn532_io;

static void pn532_init(void) {
    printf("[PN532] Initializing with HSU/UART driver...\n");
    memset(&pn532_io, 0, sizeof(pn532_io));

    esp_err_t err = pn532_new_driver_hsu(
        PN532_UART_RX, PN532_UART_TX,
        GPIO_NUM_NC,  // no reset pin
        GPIO_NUM_NC,  // no IRQ pin
        PN532_UART_PORT,
        115200,
        &pn532_io
    );
    printf("[PN532] new_driver_hsu: %s\n", esp_err_to_name(err));
    if (err != ESP_OK) { pn532_ok = false; return; }

    err = pn532_init(&pn532_io);
    printf("[PN532] init: %s\n", esp_err_to_name(err));
    if (err != ESP_OK) { pn532_ok = false; return; }

    uint32_t fw = 0;
    err = pn532_get_firmware_version(&pn532_io, &fw);
    if (err == ESP_OK) {
        printf("[PN532] Firmware: %lu.%lu\n", (fw >> 24) & 0xFF, (fw >> 16) & 0xFF);
        pn532_ok = true;
    } else {
        printf("[PN532] Firmware read failed: %s\n", esp_err_to_name(err));
        pn532_ok = false;
    }
}

// ===== WiFi =====
static void wifi_evt(void*a,esp_event_base_t b,int32_t id,void*d){
    if(b==WIFI_EVENT&&id==WIFI_EVENT_STA_START)esp_wifi_connect();
    else if(b==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){wifi_connected=false;esp_wifi_connect();}
    else if(b==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t*e=(ip_event_got_ip_t*)d;
        printf("[WiFi] IP: "IPSTR"\n",IP2STR(&e->ip_info.ip));
        wifi_connected=true;xEventGroupSetBits(wifi_event_group,WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void){
    wifi_event_group=xEventGroupCreate();
    esp_netif_init();esp_event_loop_create_default();esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_evt,NULL,NULL);
    esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&wifi_evt,NULL,NULL);
    wifi_config_t wc;memset(&wc,0,sizeof(wc));
    memcpy(wc.sta.ssid,WIFI_SSID,strlen(WIFI_SSID));
    memcpy(wc.sta.password,WIFI_PASSWORD,strlen(WIFI_PASSWORD));
    wc.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_STA);esp_wifi_set_config(WIFI_IF_STA,&wc);esp_wifi_start();
}

// ===== HTTP =====
typedef struct{int code;char*b;size_t l;}http_r;
static esp_err_t http_evt(esp_http_client_event_t*e){
    http_r*r=(http_r*)e->user_data;
    if(e->event_id==HTTP_EVENT_ON_DATA){
        if(!r->b){size_t t=esp_http_client_get_content_length(e->client);if(t<=0)t=4096;r->b=(char*)malloc(t+1);r->l=0;}
        if(r->b){size_t s=esp_http_client_get_content_length(e->client);if(s<=0)s=4096;
            size_t rem=s-r->l;if(rem>e->data_len)rem=e->data_len;
            memcpy(r->b+r->l,e->data,rem);r->l+=rem;r->b[r->l]=0;}
    }else if(e->event_id==HTTP_EVENT_ON_FINISH)r->code=esp_http_client_get_status_code(e->client);
    return ESP_OK;
}
static int api(const char*m,const char*p,const char*b,http_r*r){
    if(!wifi_connected)return -1;
    char u[256];snprintf(u,sizeof(u),"http://%s:%d%s%s",API_HOST,API_PORT,API_BASE,p);
    esp_http_client_config_t c;memset(&c,0,sizeof(c));
    c.url=u;c.method=strcmp(m,"POST")==0?HTTP_METHOD_POST:HTTP_METHOD_GET;
    c.event_handler=http_evt;c.user_data=r;c.timeout_ms=HTTP_TIMEOUT_MS;
    esp_http_client_handle_t h=esp_http_client_init(&c);
    esp_http_client_set_header(h,"X-API-Key",API_KEY);
    if(b&&strlen(b)){esp_http_client_set_header(h,"Content-Type","application/json");esp_http_client_set_post_field(h,b,strlen(b));}
    r->b=NULL;r->l=0;r->code=0;
    esp_http_client_perform(h);int co=r->code;
    esp_http_client_cleanup(h);return co;
}

// ===== Process card =====
static void process_card(const char*nfc){
    printf("[CARD] %s\n",nfc);
    if(!wifi_connected)return;
    char p[128];snprintf(p,sizeof(p),"/members/nfc/%s",nfc);http_r r;int co=api("GET",p,NULL,&r);
    if(co<=0||co==401){if(r.b)free(r.b);return;}
    if(co==404&&r.b){
        cJSON*j=cJSON_Parse(r.b);const char*d="";
        if(j){cJSON*di=cJSON_GetObjectItem(j,"detail");if(di)d=di->valuestring;}
        if(!d[0]||strstr(d,"not found")||strstr(d,"Unknown")){
            cJSON*po=cJSON_CreateObject();cJSON_AddStringToObject(po,"nfc_id",nfc);
            char*bd=cJSON_PrintUnformatted(po);http_r pr;int pc=api("POST","/pending-nfc",bd,&pr);
            cJSON_free(bd);cJSON_Delete(po);if(pr.b)free(pr.b);if(j)cJSON_Delete(j);if(r.b)free(r.b);return;
        }if(j)cJSON_Delete(j);
    }
    if(co!=200||!r.b){if(r.b)free(r.b);return;}
    cJSON*j=cJSON_Parse(r.b);if(!j){free(r.b);return;}
    bool sv=false,ip=false;const char*n="";
    cJSON*it=cJSON_GetObjectItem(j,"safety_briefing_valid");if(it)sv=it->valuedouble!=0;
    it=cJSON_GetObjectItem(j,"is_present");if(it)ip=it->valuedouble!=0;
    it=cJSON_GetObjectItem(j,"first_name");if(it&&it->valuestring)n=it->valuestring;
    if(!n||!*n){it=cJSON_GetObjectItem(j,"name");if(it&&it->valuestring)n=it->valuestring;}
    if(!n)n="";
    if(ip){
        cJSON*coj=cJSON_CreateObject();cJSON_AddStringToObject(coj,"nfc_id",nfc);
        char*bd=cJSON_PrintUnformatted(coj);http_r cr;int c=api("POST","/members/check-out",bd,&cr);
        cJSON_free(bd);cJSON_Delete(coj);if(cr.b)free(cr.b);
    }else{
        cJSON*cij=cJSON_CreateObject();cJSON_AddStringToObject(cij,"nfc_id",nfc);
        char*bd=cJSON_PrintUnformatted(cij);http_r cr;int c=api("POST","/members/check-in",bd,&cr);
        cJSON_free(bd);cJSON_Delete(cij);if(cr.b)free(cr.b);
    }
    cJSON_Delete(j);free(r.b);
}

// ===== Main =====
extern "C" void app_main(void) {
    printf("[MAIN] Starting...\n");

    // Wait 2s for serial monitor connection
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_err_t ret=nvs_flash_init();
    if(ret==ESP_ERR_NVS_NO_FREE_PAGES||ret==ESP_ERR_NVS_NEW_VERSION_FOUND){nvs_flash_erase();nvs_flash_init();}

    // SPI bus (must be before LCD)
    spi_bus_config_t b;memset(&b,0,sizeof(b));
    b.mosi_io_num=DISPLAY_MOSI;b.miso_io_num=-1;b.sclk_io_num=DISPLAY_SCLK;b.max_transfer_sz=172*320*2;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&b,SPI_DMA_CH_AUTO));

    // Display
    display_init();

    // LED
    led_init();

    // LED quick test (purple then off)
    printf("[LED] Quick test\n");
    led_set(128,0,128); vTaskDelay(pdMS_TO_TICKS(300));
    led_off();

    // Display: clear to black immediately
    disp_clear_rgb(0, 0, 0);

    // PN532
    pn532_init();

    // WiFi + SNTP time sync
    wifi_init();

    // Wait for WiFi + sync time
    xEventGroupWaitBits(wifi_event_group,WIFI_CONNECTED_BIT,false,true,pdMS_TO_TICKS(15000));

    if(wifi_connected){
        http_r hr;int h=api("GET","/health",NULL,&hr);
        printf("[API] Health:%d\n",h);if(hr.b)free(hr.b);

        // Initialize SNTP for time sync
        printf("[SNTP] Syncing time...\n");
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        // Wait for time sync
        time_t now = 0;
        struct tm tm;
        int retry = 0;
        while (tm.tm_year < (2024 - 1900) && ++retry < 20) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            time(&now);
            localtime_r(&now, &tm);
        }
        printf("[SNTP] Time synced: %s", asctime(&tm));
        // Set timezone to Europe/Berlin (CEST)
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
        printf("[SNTP] Timezone set to Europe/Berlin\n");
    }

    printf("[MAIN] Ready. Starting clock task...\n");

    // Draw black background
    disp_clear_black();

    // Start clock task (independent, checks every 500ms, updates when changed)
    xTaskCreate(clock_task, "clock", 4096, NULL, 5, NULL);

    while(1){
        // LED white pulsating
        uint32_t cycle = (esp_timer_get_time()/1000) % 4000;
        uint8_t b = cycle<2000?(cycle*255)/1999:255-((cycle-2000)*255)/1999;
        led_set(b, b, b);

        int64_t now=esp_timer_get_time();
        if(now<card_debounce_until_us||now-last_card_check_us<500000){vTaskDelay(10);continue;}
        last_card_check_us=now;
        vTaskDelay(100);
    }
}