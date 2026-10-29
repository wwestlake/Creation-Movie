cmake_minimum_required(VERSION 3.22)

set(CREATION_MOVIE_VERSION "0.0.1" CACHE STRING "Djehuti Movie product version")
project(CreationMovie VERSION ${CREATION_MOVIE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # The installed Windows SDK 19041 C++/WinRT projection uses the legacy
    # coroutine header. Isolate its required language mode to this adapter;
    # the Movie application and all other sources remain C++20.
    set_source_files_properties(Source/MovieVisualCapture.cpp PROPERTIES COMPILE_OPTIONS "/std:c++17;/await")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
endif()

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "Set JUCE_DIR to your JUCE root directory before configuring (same JUCE checkout Djehuti Station and Djehuti Engine use).")
endif()

include(cmake/CreationSharedLLVM.cmake)
configure_creation_shared_llvm(CM "Djehuti Movie")
include(cmake/MovieFFmpeg.cmake)

set(CREATION_SUITE_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared" CACHE PATH "Path to the Creation Suite shared code root")

add_subdirectory("${JUCE_DIR}" juce)
add_subdirectory(Language)

set(CM_ASSET_SYSTEM_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/AssetSystem/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/AssetSystem"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedAssetSystem")
    set(CM_ASSET_SYSTEM_TARGET creation_suite_asset_system)
endif()

set(CM_VFS_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/VFS/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/VFS"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedVFS")
    set(CM_VFS_TARGET creation_suite_vfs)
endif()

set(CM_SERVICES_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/Services/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/Services"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedServices")
    set(CM_SERVICES_TARGET creation_suite_services)
endif()

set(CM_LITESEMRAG_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/LiteSemRag/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/LiteSemRag"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedLiteSemRag")
    set(CM_LITESEMRAG_TARGET creation_suite_litesemrag)
endif()

set(CM_UI_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/UI/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/UI"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedUI")
    set(CM_UI_TARGET creation_suite_ui)
endif()

set(CM_VIRTUAL_ENGINEER_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/VirtualEngineer/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/VirtualEngineer"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedVirtualEngineer")
    set(CM_VIRTUAL_ENGINEER_TARGET creation_suite_virtual_engineer creation_suite_virtual_engineer_ui)
endif()

set(CM_TIMELINE_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/Timeline/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/Timeline"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedTimeline")
    set(CM_TIMELINE_TARGET creation_suite_timeline)
endif()

set(CM_INTEROP_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/Interop/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/Interop"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedInterop")
    set(CM_INTEROP_TARGET creation_suite_interop)
endif()

if(EXISTS "${CREATION_SUITE_SHARED_DIR}/JuceDocking/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/JuceDocking"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedDocking")
endif()

if(NOT TARGET node_system)
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/NodeSystem" CreationSuiteSharedNodeSystem)
endif()

juce_add_gui_app(CreationMovieApp
    PRODUCT_NAME "Djehuti Movie"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationmovie"
    ICON_BIG "${CREATION_SUITE_SHARED_DIR}/UI/assets/creation_movie.png"
    ICON_SMALL "${CREATION_SUITE_SHARED_DIR}/UI/assets/creation_movie.png"
)

movie_copy_ffmpeg(CreationMovieApp)

juce_generate_juce_header(CreationMovieApp)

target_include_directories(CreationMovieApp PRIVATE
    "${CREATION_SUITE_SHARED_DIR}/JuceDocking"
    "${CREATION_SUITE_SHARED_DIR}/JuceDocking/include"
)

target_sources(CreationMovieApp PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/TrackTimeline.inc
    Source/TrackInspector.inc
    Source/TrackUiCheck.inc
    Source/EffectsPanel.inc
    Source/MovieDocument.cpp
    Source/MovieRecording.cpp Source/MovieRecordingWriter.cpp
    Source/MovieTracks.cpp
    Source/MovieVisualCapture.cpp Source/MovieDeviceCapture.cpp Source/MovieVisualCapture.h
    Source/MovieTransitions.cpp Source/MovieTransitions.h Source/TransitionPanel.inc Source/MovieTransitionCards.h
    Source/MovieAutomation.cpp Source/MovieAutomation.h
    Source/MovieEffects.cpp
    Source/MovieEffects.h
    Source/MovieDocument.h
    Source/MovieMedia.cpp
    Source/MovieMedia.h
    Source/MoviePlayback.cpp
    Source/MoviePlayback.h
    Source/MovieExport.cpp
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
    ${CM_ASSET_SYSTEM_TARGET}
    ${CM_VFS_TARGET}
    ${CM_SERVICES_TARGET}
    ${CM_LITESEMRAG_TARGET}
    ${CM_VIRTUAL_ENGINEER_TARGET}
    ${CM_TIMELINE_TARGET}
    ${CM_UI_TARGET}
    ${CM_INTEROP_TARGET}
    movie_ffmpeg
    creation_movie_language_host
    node_system
    CreationDock
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(WIN32)
    target_link_libraries(CreationMovieApp PRIVATE windowsapp d3d11 dxgi dwmapi mfplat mfreadwrite mfuuid mf ole32 propsys)
endif()

install(TARGETS CreationMovieApp
    RUNTIME DESTINATION .
    RESOURCE DESTINATION .
)
install(FILES README.md LICENSE docs/EULA.txt DESTINATION .)

set(CPACK_PACKAGE_NAME "Djehuti Movie")
set(CPACK_PACKAGE_VENDOR "LagDaemon Software")
set(CPACK_PACKAGE_CONTACT "LagDaemon Software")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Djehuti Movie")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "djehuti-movie-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Djehuti Movie")
set(CPACK_GENERATOR "ZIP;WIX")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "Djehuti Movie")
set(CPACK_WIX_LICENSE_RTF "${CMAKE_CURRENT_SOURCE_DIR}/installer/EULA.rtf")
set(CPACK_PACKAGE_EXECUTABLES "Djehuti Movie" "Djehuti Movie")
set(CPACK_CREATE_DESKTOP_LINKS "Djehuti Movie")

include(CPack)

add_executable(MovieDocumentTests tests/MovieDocumentTests.cpp Source/MovieDocument.cpp Source/MovieTracks.cpp Source/MovieEffects.cpp Source/MovieAutomation.cpp Source/MovieTransitions.cpp)
target_include_directories(MovieDocumentTests PRIVATE Source)
target_link_libraries(MovieDocumentTests PRIVATE creation_suite_timeline juce::juce_data_structures juce::juce_audio_basics juce::juce_gui_basics)
target_compile_definitions(MovieDocumentTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

if(WIN32)
    add_executable(MovieVisualCaptureTests tests/VisualCaptureTests.cpp Source/MovieVisualCapture.cpp Source/MovieDeviceCapture.cpp)
    target_include_directories(MovieVisualCaptureTests PRIVATE Source)
    target_link_libraries(MovieVisualCaptureTests PRIVATE juce::juce_gui_basics windowsapp d3d11 dxgi dwmapi mfplat mfreadwrite mfuuid mf ole32 propsys)
    target_compile_definitions(MovieVisualCaptureTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
endif()
enable_testing()
add_test(NAME movie_document_tracks COMMAND MovieDocumentTests)

# Optional local integration validation, using the canonical Movie build tree.
# Normal Movie builds consume the service client without building its server.
option(CREATION_MOVIE_BUILD_VFS_SERVICE "Build the VFS service and streaming checks alongside Movie" OFF)
if(CREATION_MOVIE_BUILD_VFS_SERVICE)
    set(CREATION_VFS_COPY_SHARED_BIN OFF)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../services/VfsService" "${CMAKE_CURRENT_BINARY_DIR}/VfsService")
endif()
