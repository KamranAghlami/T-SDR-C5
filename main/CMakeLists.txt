if(IDF_TARGET STREQUAL "esp32s3")
    set(receiver s3_radio)
else()
    set(receiver c5_radio)
endif()

idf_component_register(SRCS "main.cpp" "settings.cpp"
    PRIV_REQUIRES board ${receiver} core ui esp_app_format esp_timer heap nvs_flash)
