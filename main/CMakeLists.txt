idf_component_register(SRCS "telemetry.pb.c" "app_main.c" "pb_common.c" "pb_encode.c"
                    PRIV_REQUIRES mqtt nvs_flash json esp_netif esp_wifi driver
                    INCLUDE_DIRS ".")
