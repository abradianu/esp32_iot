idf_component_register(SRCS "sensors.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver hdc1080 sht4x)
