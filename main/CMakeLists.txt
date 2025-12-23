idf_component_register(SRCS "app_main.c"
                    PRIV_REQUIRES mqtt nvs_flash json esp_netif esp_wifi driver
                    INCLUDE_DIRS ".")
