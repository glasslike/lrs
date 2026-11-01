cmake_minimum_required(VERSION 3.10)

# Package version. The only place to change it, the same role as
# AC_INIT's version argument in qico. A release is "0.1.0". A
# development tree ends in "-dev"; configure then appends -g and the
# git abbreviation, so the ident reads 0.1.1-dev-g0123456. CMake's
# project(VERSION) accepts only numbers, so the string lives here.
set(LRS_VERSION "0.1.0-dev")
set(LRS_NAME "lrs")

project(lrs C)

include(cmake/Version.cmake)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(lrs
    src/util.c
    src/json.c
    src/config.c
    src/squish.c
    src/jam.c
    src/base.c
    src/lrpfile.c
    src/state.c
    src/sync.c
    src/selftest.c
    src/main.c
)

# The build directory holds the generated version.h. It is listed before
# src so that header is the one the compiler finds.
target_include_directories(lrs PRIVATE ${CMAKE_BINARY_DIR} src)

if(MSVC)
    target_compile_definitions(lrs PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(lrs PRIVATE /W3)
else()
    target_compile_options(lrs PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# -std=c11 hides POSIX prototypes. Debian then warns that fileno (used to
# flock a Squish base) is undeclared. The macro is applied on the command
# line, before any header, which is the only place glibc honors it.
if(UNIX)
    target_compile_definitions(lrs PRIVATE _POSIX_C_SOURCE=200809L)
endif()

enable_testing()
add_test(NAME selftest COMMAND lrs selftest)

# Install only the binary. The config stays in whatever directory the
# operator keeps it and is passed with "lrs -c /that/dir/lrs.conf".
# CMAKE_INSTALL_BINDIR is "bin" under CMAKE_INSTALL_PREFIX, so
# -DCMAKE_INSTALL_PREFIX=/usr/local puts the program at /usr/local/bin/lrs.
# BINDIR "." means the prefix itself. CMake would otherwise record the
# destination as "${prefix}/./lrs", which is the same file but reads as a
# different directory in the install log.
include(GNUInstallDirs)
if(CMAKE_INSTALL_BINDIR STREQUAL "." OR CMAKE_INSTALL_BINDIR STREQUAL "./")
    install(TARGETS lrs RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})
else()
    install(TARGETS lrs RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
