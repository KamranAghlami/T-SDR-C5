set(espsdr "${CMAKE_CURRENT_LIST_DIR}/../../third_party/esp-sdr/main")
if(NOT EXISTS "${espsdr}/common/rx_recalibration.c")
    message(FATAL_ERROR "third_party/esp-sdr is missing: run git submodule update --init")
endif()
if(NOT IDF_TARGET STREQUAL "esp32c5")
    message(FATAL_ERROR "c5_radio supports only esp32c5")
endif()

idf_component_register(
    SRCS "c5_radio.c"
         "${espsdr}/common/rx_recalibration.c"
         "${espsdr}/common/rx_calibration_state.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${espsdr}/common" "${espsdr}/targets/esp32c5"
    PRIV_REQUIRES esp_wifi esp_phy esp_event esp_timer esp_hw_support heap nvs_flash)

# Keep static sections out of the RF dump bank (upstream linker guard).
target_link_options(${COMPONENT_LIB} INTERFACE "-T${espsdr}/targets/esp32c5/sram_guard.ld")
# Retune hooks: measure DC at the requested LO during recalibration.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=phy_chip_set_chan_ana")
include("${espsdr}/common/rx_calibration_link.cmake")
