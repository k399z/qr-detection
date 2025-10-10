cmake_minimum_required(VERSION 3.16)

project(qr_detection LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui objdetect videoio)

# Main webcam QR detector (from main.cpp at repo root)
add_executable(detector ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(detector PRIVATE ${OpenCV_LIBS})

# Optional: QR code generator GUI (requires libqrencode)
include(CheckIncludeFile)
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(QRENCODE QUIET libqrencode)
endif()

if (QRENCODE_FOUND)
    add_executable(qrcode_generator ${CMAKE_CURRENT_SOURCE_DIR}/tools/qrcode_generator.cpp)
    target_include_directories(qrcode_generator PRIVATE ${QRENCODE_INCLUDE_DIRS})
    target_link_libraries(qrcode_generator PRIVATE ${OpenCV_LIBS} ${QRENCODE_LIBRARIES})
    target_compile_options(qrcode_generator PRIVATE ${QRENCODE_CFLAGS_OTHER})
else()
    message(STATUS "libqrencode not found; skipping qrcode_generator target. Install libqrencode-dev to enable.")
endif()

# Tests directory is optional
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    option(ENABLE_TESTS "Build unit tests" ON)
    if (ENABLE_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()
