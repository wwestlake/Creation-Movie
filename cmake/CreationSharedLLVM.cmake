function(configure_creation_shared_llvm APP_PREFIX APP_DISPLAY_NAME)
    option(${APP_PREFIX}_ENABLE_SCRIPTING "Build ${APP_DISPLAY_NAME}'s LLVM-backed language host layer" ON)

    if(NOT ${APP_PREFIX}_ENABLE_SCRIPTING)
        return()
    endif()

    set(_local_vcpkg_dir "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows")
    set(_engine_vcpkg_dir "D:/000 Creation Engine/vcpkg_installed/x64-windows")

    if(EXISTS "${_local_vcpkg_dir}")
        set(_llvm_root "${_local_vcpkg_dir}")
    elseif(EXISTS "${_engine_vcpkg_dir}")
        set(_llvm_root "${_engine_vcpkg_dir}")
    else()
        message(FATAL_ERROR
            "${APP_PREFIX}_ENABLE_SCRIPTING is ON but no LLVM install was found.\n"
            "Expected either ${_local_vcpkg_dir} (this repo's manifest install) or\n"
            "${_engine_vcpkg_dir} (the already-built shared Creation Engine LLVM install).")
    endif()

    set(${APP_PREFIX}_LLVM_ROOT "${_llvm_root}" PARENT_SCOPE)
    list(APPEND CMAKE_PREFIX_PATH "${_llvm_root}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    find_package(LLVM CONFIG REQUIRED)
    message(STATUS "${APP_DISPLAY_NAME}: using LLVM ${LLVM_PACKAGE_VERSION} from ${_llvm_root}")
endfunction()
