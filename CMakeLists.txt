cmake_minimum_required(VERSION 3.15)
project(chronicle C)

find_package(ZLIB REQUIRED)
find_path(LIBGIT2_INCLUDE_DIR NAMES git2.h)
find_library(LIBGIT2_LIBRARY NAMES git2)
find_library(PCRE_LIBRARY NAMES pcre pcre2-8 pcre2-8-static pcre2-8d)

add_executable(chronicle chronicle.c)

target_include_directories(chronicle PRIVATE 
    ${LIBGIT2_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(chronicle PRIVATE 
    ${LIBGIT2_LIBRARY}
    ZLIB::ZLIB
    ${PCRE_LIBRARY}
    rpcrt4.lib
    crypt32.lib
    winhttp.lib
    advapi32.lib
    ole32.lib
    user32.lib
    credui.lib
)