include(FetchContent)
find_package(OpenSSL REQUIRED)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(NO_MEDIA OFF CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)
FetchContent_Declare(movie_datachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG d9617a53201615ef729e99dc39ad6736982d0346
    GIT_CONFIG core.fsmonitor=false
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(movie_datachannel)

function(movie_copy_webcam_runtime target)
    if(WIN32)
        foreach(component crypto ssl)
            find_file(MOVIE_OPENSSL_${component}_DLL NAMES lib${component}-3-x64.dll
                HINTS "${OPENSSL_ROOT_DIR}/bin" "${OPENSSL_INCLUDE_DIR}/../bin")
            if(MOVIE_OPENSSL_${component}_DLL)
                add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MOVIE_OPENSSL_${component}_DLL}" "$<TARGET_FILE_DIR:${target}>")
                if(target STREQUAL "CreationMovieApp")
                    install(FILES "${MOVIE_OPENSSL_${component}_DLL}" DESTINATION .)
                endif()
            endif()
        endforeach()
    endif()
endfunction()
