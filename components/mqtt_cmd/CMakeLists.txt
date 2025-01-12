idf_component_register(SRCS "mqtt_cmd.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_utils json main mqtt ota)
