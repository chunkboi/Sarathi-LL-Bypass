cmake_minimum_required(VERSION 3.25)

project(seb_keys VERSION 2.0.0 LANGUAGES CXX)

set(SEB_CXX_STANDARD "26" CACHE STRING "C++ standard used to build seb_keys (20, 23 or 26)")
set(CMAKE_CXX_STANDARD "${SEB_CXX_STANDARD}")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT WIN32)
    message(FATAL_ERROR "seb_keys uses Windows NT/Win32 APIs and can only be built on Windows")
endif()

add_executable(seb_keys src/main.cpp)

if(MSVC)
    target_compile_definitions(seb_keys PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS)

    target_compile_options(seb_keys PRIVATE
        /utf-8
        /W4
        /permissive-
        /EHsc
        /guard:cf
        $<$<CONFIG:Release>:/O2 /GL>
        /Gy
        /Gw)

    target_link_options(seb_keys PRIVATE
        /guard:cf
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
        $<$<CONFIG:Release>:-O3 -march=x86-64-v3>
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -fno-strict-aliasing
        -fstack-protector-strong
        -ffunction-sections
        -fdata-sections
        $<$<CXX_COMPILER_ID:Clang>:-mguard=cf -ftrivial-auto-var-init=zero $<$<CONFIG:Release>:-flto>>)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_options(seb_keys PRIVATE
            -fuse-ld=lld
            $<$<CONFIG:Release>:-flto>
            -s
            -Wl,--gc-sections,--no-insert-timestamp
            -static
            -static-libgcc
            -static-libstdc++
            -pthread)
    else()
        target_link_options(seb_keys PRIVATE
            -s
            -Wl,--gc-sections,--no-insert-timestamp
            -static
            -static-libgcc
            -static-libstdc++
            -pthread)
    endif()
endif()

target_link_libraries(seb_keys PRIVATE
    ntdll
    bcrypt
    crypt32
    version
    ws2_32
    advapi32
    shell32)

set_target_properties(seb_keys PROPERTIES OUTPUT_NAME "seb_keys")

message(STATUS "seb_keys: C++${CMAKE_CXX_STANDARD}, build type ${CMAKE_BUILD_TYPE}")
