# Version stamp for lrs. The -dev commit suffix follows qico's
# configure.ac. The ident is the version string only.
#
# LRS_VERSION is set in the top CMakeLists.txt before this file is
# included. It is the package version and nothing else: a release is
# "0.1.0", a development tree ends in "-dev".
#
# LRS_VERSION_ID is baked at configure time, not at compile time, so
# two builds of the same configured tree print the same ident. It is
# LRS_VERSION, or LRS_VERSION-g<abbrev> when the package version ends
# in -dev and a commit id was found. A release string is left unchanged.
# --version prints this. --help prints LRS_VERSION, so the help text
# does not change on every commit.
#
# The abbreviation comes from git when the source tree is a checkout,
# else from src/source-commit, which a source tarball can carry the way
# qico's dist-hook writes it.
#
# A later commit is picked up the next time CMake configures. A rebuild
# alone does not ask git again.

set(LRS_GIT_ABBREV "")

# .git may be a directory or a file (a linked work tree). Either one
# means "ask git"; a plain exported tree has neither.
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    find_package(Git QUIET)
endif()

if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --is-inside-work-tree
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE _lrs_git_rc
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(_lrs_git_rc EQUAL 0)
        # Seven hex digits is the abbreviation git uses for a small
        # repository. --short=7 still prints more when those seven are
        # not unique, and that longer prefix is kept.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _lrs_rev_rc
            OUTPUT_VARIABLE _lrs_rev
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_lrs_rev_rc EQUAL 0)
            set(LRS_GIT_ABBREV "${_lrs_rev}")
        endif()
    endif()
endif()

# Tarball fallback. Not a checkout file. It exists only when whoever
# packed the sources wrote it, as qico's dist-hook writes
# src/source-commit into the dist tree.
if(LRS_GIT_ABBREV STREQUAL "" AND EXISTS "${CMAKE_SOURCE_DIR}/src/source-commit")
    file(READ "${CMAKE_SOURCE_DIR}/src/source-commit" _lrs_file_rev)
    string(STRIP "${_lrs_file_rev}" LRS_GIT_ABBREV)
endif()

# Anything other than hex is not an abbreviation this build records.
# Fewer than seven digits is dropped as well.
if(NOT LRS_GIT_ABBREV MATCHES "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]+$")
    set(LRS_GIT_ABBREV "")
endif()

set(LRS_VERSION_ID "${LRS_VERSION}")
if(LRS_VERSION MATCHES "-dev$")
    if(NOT LRS_GIT_ABBREV STREQUAL "")
        set(LRS_VERSION_ID "${LRS_VERSION}-g${LRS_GIT_ABBREV}")
    endif()
endif()
if(LRS_VERSION_ID MATCHES "[\"\\\\]")
    set(LRS_VERSION_ID "${LRS_VERSION}")
endif()

message(STATUS "lrs ${LRS_VERSION_ID}")

configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_BINARY_DIR}/version.h"
    @ONLY
)
