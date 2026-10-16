# Pure C++ logic (no ESP-IDF APIs); tests/host builds the same sources.
idf_component_register(SRCS "src/fft.cpp" "src/gain.cpp" "src/gestures.cpp" "src/menu.cpp" "src/spectrum.cpp" "src/survey.cpp"
    INCLUDE_DIRS "include")
