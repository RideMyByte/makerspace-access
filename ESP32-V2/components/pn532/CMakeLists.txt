idf_component_register(SRCS "src/pn532_driver.c" "src/pn532_driver_spi.c" "src/pn532_driver_hsu.c" "src/pn532.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_timer)
