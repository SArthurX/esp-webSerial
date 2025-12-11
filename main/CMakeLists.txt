idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash esp_http_server nvs_flash esp_wifi driver
                       INCLUDE_DIRS ".")
