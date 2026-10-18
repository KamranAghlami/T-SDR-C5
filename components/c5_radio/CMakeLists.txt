if(NOT IDF_TARGET STREQUAL "esp32c5")
    # Receiver of the T-Display-C5 build only; main picks the one for the target.
    idf_component_register()
    return()
endif()

set(espsdr "${CMAKE_CURRENT_LIST_DIR}/../../third_party/esp-sdr/main")
if(NOT EXISTS "${espsdr}/common/rx_recalibration.c")
    message(FATAL_ERROR "third_party/esp-sdr is missing: run git submodule update --init")
endif()

idf_component_register(
    SRCS "c5_radio.c" "c5_engine.c"
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
