idf_component_register(SRCS "src/renderer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board core
    PRIV_REQUIRES esp_timer heap)
