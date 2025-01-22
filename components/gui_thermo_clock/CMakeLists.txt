set(srcs "gui.c")
list(APPEND srcs "dseg7_classic_bold_60.c")
list(APPEND srcs "dseg7_classic_bold_80.c")
list(APPEND srcs "icon_temperature_in.c")
list(APPEND srcs "icon_temperature_out.c")
list(APPEND srcs "icon_humidity_in.c")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "."
                    REQUIRES esp_lvgl_port cpu_temp_sensor nvs_utils wifi main app_update)
