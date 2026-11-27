cmake_minimum_required(VERSION 3.25)

project(seb_keys VERSION 2.0.0 LANGUAGES CXX)

# C++26 by default (Clang 22+). Override with -DSEB_CXX_STANDARD=23 if needed.
set(SEB_CXX_STANDARD "26" CACHE STRING "C++ standard used to build seb_keys (20, 23 or 26)")
set(CMAKE_CXX_STANDARD "${SEB_CXX_STANDARD}")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(seb_keys src/main.cpp)

if(MSVC)
    target_compile_definitions(seb_keys PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS)

    # /utf-8 is required: the embedded defuser JavaScript contains non-ASCII
    # glyphs and must be encoded identically on every machine.
    target_compile_options(seb_keys PRIVATE
        /utf-8
        /W4
        /permissive-
        /EHsc
        $<$<CONFIG:Release>:/O2>
        /Gy
        /Gw)

    target_link_options(seb_keys PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        /OPT:REF
        /OPT:ICF)
else()
    target_compile_definitions(seb_keys PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        _GLIBCXX_ASSERTIONS
        _CRT_SECURE_NO_WARNINGS)

    target_compile_options(seb_keys PRIVATE
        $<$<CONFIG:Release>:-O3>
        -Wall
        -Wextra
        -Wpedantic
        -fno-strict-aliasing
        -fstack-protector-strong
        -ffunction-sections
        -fdata-sections
        $<$<CXX_COMPILER_ID:Clang>:-mguard=cf -ftrivial-auto-var-init=zero>)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_options(seb_keys PRIVATE -fuse-ld=lld -s -Wl,--gc-sections -static -static-libgcc -static-libstdc++ -pthread)
    else()
        target_link_options(seb_keys PRIVATE -s -Wl,--gc-sections -static -static-libgcc -static-libstdc++ -pthread)
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT SEB_IPO_SUPPORTED OUTPUT SEB_IPO_ERROR LANGUAGES CXX)
if(SEB_IPO_SUPPORTED)
    set_property(TARGET seb_keys PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "IPO/LTO not available: ${SEB_IPO_ERROR}")
endif()

if(WIN32)
    target_link_libraries(seb_keys PRIVATE
        ntdll
        bcrypt
        crypt32
        version
        ws2_32
        advapi32
        shell32)
else()
    message(FATAL_ERROR "seb_keys uses Windows NT/Win32 APIs and can only be built on Windows")
endif()

set_target_properties(seb_keys PROPERTIES OUTPUT_NAME "seb_keys")

message(STATUS "seb_keys: C++${CMAKE_CXX_STANDARD}, build type ${CMAKE_BUILD_TYPE}")
