idf_component_register(SRCS "main.cpp" "font8x13.c" "font_big.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd driver nvs_flash esp_wifi esp_event esp_http_client esp_timer esp_sntp
