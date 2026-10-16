cmake_minimum_required(VERSION 3.22)
project(t_sdr_c5_host_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(core "${CMAKE_CURRENT_LIST_DIR}/../../components/core")

add_executable(core_tests
    core_tests.cpp
    "${core}/src/fft.cpp"
    "${core}/src/gain.cpp"
    "${core}/src/gestures.cpp"
    "${core}/src/menu.cpp"
    "${core}/src/spectrum.cpp"
    "${core}/src/survey.cpp")
target_include_directories(core_tests PRIVATE "${core}/include")
target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
target_link_options(core_tests PRIVATE -fsanitize=address,undefined)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
