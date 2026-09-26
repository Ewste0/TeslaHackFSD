cmake_minimum_required(VERSION 3.21)

project(TigardMapsInstallerOpen VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

if(WIN32)
    set(MSYS2_MINGW_ROOT "C:/msys64/mingw64" CACHE PATH
        "Path to the MSYS2 MinGW64 prefix containing libftdi1 and libusb")

    set(FTDI_INCLUDE_DIR "${MSYS2_MINGW_ROOT}/include/libftdi1")
    set(USB_INCLUDE_DIR "${MSYS2_MINGW_ROOT}/include/libusb-1.0")
    set(FTDI_LIBRARY "${MSYS2_MINGW_ROOT}/lib/libftdi1.a")
    set(USB_LIBRARY "${MSYS2_MINGW_ROOT}/lib/libusb-1.0.a")

    foreach(required_path IN ITEMS
            "${FTDI_INCLUDE_DIR}/ftdi.h"
            "${USB_INCLUDE_DIR}/libusb.h"
            "${FTDI_LIBRARY}"
            "${USB_LIBRARY}")
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR "Required dependency not found: ${required_path}")
        endif()
    endforeach()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFTDI REQUIRED IMPORTED_TARGET libftdi1)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
endif()

add_executable(TigardMapsInstaller WIN32
    src/main.cpp
    src/Core.h
)

target_include_directories(TigardMapsInstaller PRIVATE src)
target_link_libraries(TigardMapsInstaller PRIVATE Qt6::Widgets Qt6::Network)

if(WIN32)
    target_include_directories(TigardMapsInstaller PRIVATE
        "${FTDI_INCLUDE_DIR}"
        "${USB_INCLUDE_DIR}"
    )
    target_link_libraries(TigardMapsInstaller PRIVATE
        "${FTDI_LIBRARY}"
        "${USB_LIBRARY}"
    )
else()
    target_link_libraries(TigardMapsInstaller PRIVATE
        PkgConfig::LIBFTDI
        PkgConfig::LIBUSB
    )
endif()

install(TARGETS TigardMapsInstaller RUNTIME DESTINATION bin BUNDLE DESTINATION .)
