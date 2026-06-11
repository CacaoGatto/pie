cmake_minimum_required(VERSION 3.16)
project(pie VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# C++ standard
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# Output layout
# ============================================================================
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test)

# ============================================================================
# Pie library — both static and shared
# ============================================================================
add_library(pie STATIC src/pie.cc)
add_library(pie_shared SHARED src/pie.cc)

set_target_properties(pie_shared PROPERTIES OUTPUT_NAME pie)

foreach(_tgt pie pie_shared)
    target_include_directories(${_tgt} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
endforeach()

# ============================================================================
# Test executables (optional, off by default — built via rum's test tree)
# ============================================================================
option(PIE_BUILD_TESTS "Build pie standalone test executables" OFF)
if(PIE_BUILD_TESTS)
    add_subdirectory(test)
endif()
