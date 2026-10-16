idf_component_register(SRCS "main.cpp" "settings.cpp"
    PRIV_REQUIRES board c5_radio core ui esp_app_format esp_timer heap nvs_flash)
