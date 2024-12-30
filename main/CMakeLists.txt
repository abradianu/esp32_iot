idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi nvs_utils http_server display gui_thermo_clock hdc1080)
