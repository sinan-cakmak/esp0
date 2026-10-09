idf_component_register(SRCS "main.c" "controller_logic.c" "wiz_protocol.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_wifi nvs_flash esp_event esp_netif driver lwip json)
