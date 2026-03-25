idf_component_register(
    SRCS
        "main.cpp"
        "telemetryReceiverTask.cpp"
        "sensorEmulatorTask.cpp"
        "ledTask.cpp"
    INCLUDE_DIRS "." "../tests/aggregatorClass"
    REQUIRES
        driver
        esp_timer
        crc16
)
