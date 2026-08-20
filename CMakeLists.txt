cmake_minimum_required(VERSION 3.22)

set(CREATION_MOVIE_VERSION "0.0.1" CACHE STRING "Creation Movie product version")
project(CreationMovie VERSION ${CREATION_MOVIE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/CMake/CreationSuiteBuildSettings.cmake")
creation_suite_require_juce()

if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
endif()

include(cmake/CreationSharedLLVM.cmake)
configure_creation_shared_llvm(CM "Creation Movie")

add_subdirectory("${JUCE_DIR}" juce)
add_subdirectory(Language)

juce_add_gui_app(CreationMovieApp
    PRODUCT_NAME "Creation Movie"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationmovie"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/UI/assets/creation_movie.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/UI/assets/creation_movie.png"
)

creation_suite_add_shared_bin_copy(CreationMovieApp)

juce_generate_juce_header(CreationMovieApp)

target_sources(CreationMovieApp PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/Legal/EulaText.cpp
    Source/Legal/EulaText.h
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

install(TARGETS CreationMovieApp
    RUNTIME DESTINATION .
    RESOURCE DESTINATION .
)
install(FILES README.md LICENSE docs/EULA.txt DESTINATION .)

set(CPACK_PACKAGE_NAME "Creation Movie")
set(CPACK_PACKAGE_VENDOR "LagDaemon Software")
set(CPACK_PACKAGE_CONTACT "LagDaemon Software")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Creation Movie")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "creation-movie-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Creation Movie")
set(CPACK_GENERATOR "ZIP;WIX")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "Creation Movie")
set(CPACK_WIX_LICENSE_RTF "${CMAKE_CURRENT_SOURCE_DIR}/installer/EULA.rtf")
set(CPACK_PACKAGE_EXECUTABLES "Creation Movie" "Creation Movie")
set(CPACK_CREATE_DESKTOP_LINKS "Creation Movie")

include(CPack)
