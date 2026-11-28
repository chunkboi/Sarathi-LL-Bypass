cmake_minimum_required(VERSION 3.25)

project(seb_keys VERSION 2.0.0 LANGUAGES CXX)

set(SEB_CXX_STANDARD "26" CACHE STRING "C++ standard used to build seb_keys (20, 23 or 26)")
option(SEB_MINGW_TARGET "Target x86_64-w64-mingw32 when building with Clang" ON)
option(SEB_CFG_FUNCTION_TABLE "Enable full 0x10500 CFG FID table flags (for llvm-mingw CRT/libc++)" OFF)

if(SEB_CXX_STANDARD STREQUAL "26" AND CMAKE_VERSION VERSION_LESS "3.29")
    set(CMAKE_CXX_STANDARD "23")
    set(_SEB_FORCE_CXX26 ON)
else()
    set(CMAKE_CXX_STANDARD "${SEB_CXX_STANDARD}")
    set(_SEB_FORCE_CXX26 OFF)
endif()
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
        $<$<BOOL:${_SEB_FORCE_CXX26}>:/std:c++latest>
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
        _CRT_SECURE_NO_WARNINGS
        $<$<BOOL:${SEB_CFG_FUNCTION_TABLE}>:SEB_CFG_FUNCTION_TABLE=1>)

    target_compile_options(seb_keys PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:Clang>,$<BOOL:${SEB_MINGW_TARGET}>>:--target=x86_64-w64-mingw32>
        $<$<BOOL:${_SEB_FORCE_CXX26}>:-std=c++26>
        $<$<CONFIG:Release>:-O3 -march=x86-64-v3>
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wsign-conversion
        -Wformat=2
        -Wundef
        -Wnull-dereference
        -fno-strict-aliasing
        -fstack-protector-strong
        -ffunction-sections
        -fdata-sections
        $<$<CXX_COMPILER_ID:Clang>:-mguard=cf -ftrivial-auto-var-init=zero $<$<CONFIG:Release>:-flto>>)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_options(seb_keys PRIVATE
            $<$<BOOL:${SEB_MINGW_TARGET}>:--target=x86_64-w64-mingw32>
            -fuse-ld=lld
            $<$<CONFIG:Release>:-flto>
            -s
            -Wl,--gc-sections,--no-insert-timestamp,--nxcompat,--dynamicbase,--high-entropy-va,--tsaware,-Xlink=-merge:.00cfg=.rdata
            -static
            -static-libgcc
            -static-libstdc++
            -pthread)
    else()
        target_link_options(seb_keys PRIVATE
            -s
            -Wl,--gc-sections,--no-insert-timestamp,--nxcompat,--dynamicbase,--high-entropy-va,--tsaware
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

message(STATUS "seb_keys: C++${SEB_CXX_STANDARD}, build type ${CMAKE_BUILD_TYPE}")
