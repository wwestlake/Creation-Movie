add_library(creation_movie_language_host STATIC
    AppLanguagePolicy.cpp
    AppLanguagePolicy.h
)

target_include_directories(creation_movie_language_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(CM_ENABLE_SCRIPTING)
    target_include_directories(creation_movie_language_host PRIVATE "${CM_LLVM_ROOT}/include")
    target_compile_definitions(creation_movie_language_host PUBLIC CM_ENABLE_SCRIPTING=1)
else()
    target_compile_definitions(creation_movie_language_host PUBLIC CM_ENABLE_SCRIPTING=0)
endif()
