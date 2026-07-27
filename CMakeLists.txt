cmake_minimum_required(VERSION 3.22)

set(CREATION_MOVIE_VERSION "0.0.1" CACHE STRING "Creation Movie product version")
project(CreationMovie VERSION ${CREATION_MOVIE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
endif()

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "Set JUCE_DIR to your JUCE root directory before configuring (same JUCE checkout Creation Station and Creation Engine use).")
endif()

include(cmake/CreationSharedLLVM.cmake)
configure_creation_shared_llvm(CM "Creation Movie")

add_subdirectory("${JUCE_DIR}" juce)
add_subdirectory(Language)

juce_add_gui_app(CreationMovieApp
    PRODUCT_NAME "Creation Movie"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationmovie"
)

juce_generate_juce_header(CreationMovieApp)

target_sources(CreationMovieApp PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/Branding.cpp
    Source/Branding.h
)

target_compile_definitions(CreationMovieApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CreationMovieApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CreationMovieApp,JUCE_VERSION>"
)

target_link_libraries(CreationMovieApp PRIVATE
    creation_movie_language_host
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

