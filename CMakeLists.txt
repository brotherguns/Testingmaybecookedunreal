cmake_minimum_required(VERSION 3.15...4.0)

# Ninja generator does not support platform specification
if(CMAKE_GENERATOR MATCHES "Ninja")
    # Remove all variables related to platform
    unset(CMAKE_GENERATOR_PLATFORM CACHE)
    unset(CMAKE_GENERATOR_PLATFORM)
    unset(CMAKE_VS_PLATFORM_NAME CACHE)
    unset(CMAKE_VS_PLATFORM_NAME)
    message(STATUS "Using Ninja generator - platform specification disabled")
endif()

project(Dumper-7 LANGUAGES CXX C)

# Enable Objective-C++ on Apple platforms (needed for ios_main.mm)
if(APPLE)
    enable_language(OBJCXX)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

# Source files - automatic search for all source files
file(GLOB_RECURSE CPP_SOURCES 
    "Dumper/*.cpp"
    "Dumper/*.cxx"
    "Dumper/*.cc"
    "Dumper/*.c"
    "Dumper/*.mm"
)

add_library(${PROJECT_NAME} SHARED ${CPP_SOURCES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
# Dumper
    ${CMAKE_SOURCE_DIR}/Dumper

    # Engine
    ${CMAKE_SOURCE_DIR}/Dumper/Engine
    ${CMAKE_SOURCE_DIR}/Dumper/Engine/Private
    ${CMAKE_SOURCE_DIR}/Dumper/Engine/Private/OffsetFinder
    ${CMAKE_SOURCE_DIR}/Dumper/Engine/Private/Unreal

    # Engine Public
    ${CMAKE_SOURCE_DIR}/Dumper/Engine/Public
    ${CMAKE_SOURCE_DIR}/Dumper/Engine/Public/OffsetFinder
    ${CMAKE_SOURCE_DIR}/Dumper/Engine/Public/Unreal

    # Generator
    ${CMAKE_SOURCE_DIR}/Dumper/Generator
    ${CMAKE_SOURCE_DIR}/Dumper/Generator/Private
    ${CMAKE_SOURCE_DIR}/Dumper/Generator/Private/Generators
    ${CMAKE_SOURCE_DIR}/Dumper/Generator/Private/Managers
    ${CMAKE_SOURCE_DIR}/Dumper/Generator/Private/Wrappers

    # Generator Public
    ${CMAKE_SOURCE_DIR}/Dumper/Generator/Public
    ${CMAKE_SOURCE_DIR}/Dumper/Generator/Public/Generators
    ${CMAKE_SOURCE_DIR}/Dumper/Generator/Public/Managers
    ${CMAKE_SOURCE_DIR}/Dumper/Generator/Public/Wrappers

    # Platform
    ${CMAKE_SOURCE_DIR}/Dumper/Platform
    ${CMAKE_SOURCE_DIR}/Dumper/Platform/Private

    # Platform Public
    ${CMAKE_SOURCE_DIR}/Dumper/Platform/Public

    # Utils
    ${CMAKE_SOURCE_DIR}/Dumper/Utils
    ${CMAKE_SOURCE_DIR}/Dumper/Utils/Compression
    ${CMAKE_SOURCE_DIR}/Dumper/Utils/Dumpspace
    ${CMAKE_SOURCE_DIR}/Dumper/Utils/Encoding
    ${CMAKE_SOURCE_DIR}/Dumper/Utils/Json
)

# Compiler definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CONSOLE WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        VS_GLOBAL_KEYWORD "Win32Proj"
    )
endif()

# Link Apple system frameworks and set dylib suffix
if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Foundation")
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".dylib")
endif()