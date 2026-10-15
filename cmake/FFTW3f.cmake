# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Abraham Joseph Okayli Masaryk
# FFTW3f (single precision), linked statically into the bundle.
#
#   -DFFTW_DIR=<prefix>   lib/libfftw3f.a and include/fftw3.h, e.g. a universal
#                         (arm64 + x86_64) build for release artefacts
#   otherwise             Homebrew's fftw (brew install fftw): its static
#                         archive, else the shared library
#
# Homebrew's archive is single-architecture, so a universal build needs FFTW_DIR.
set(FFTW_DIR "" CACHE PATH "Prefix of a prebuilt FFTW3f (lib/libfftw3f.a, include/fftw3.h)")
if(FFTW_DIR AND EXISTS "${FFTW_DIR}/lib/libfftw3f.a")
    set(FFTW3F_LIB "${FFTW_DIR}/lib/libfftw3f.a")
    set(FFTW3F_INC "${FFTW_DIR}/include")
else()
    list(LENGTH CMAKE_OSX_ARCHITECTURES _fftw_narch)
    if(_fftw_narch GREATER 1)
        message(FATAL_ERROR "${PROJECT_NAME}: a universal build needs -DFFTW_DIR=<prefix of a universal static FFTW3f>")
    endif()
    set(_fftw_saved_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_library(FFTW3F_STATIC fftw3f HINTS /opt/homebrew/opt/fftw/lib /usr/local/opt/fftw/lib /usr/local/lib)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_fftw_saved_suffixes})
    if(FFTW3F_STATIC)
        set(FFTW3F_LIB "${FFTW3F_STATIC}")
    else()
        find_library(FFTW3F_LIB fftw3f HINTS /opt/homebrew/opt/fftw/lib /usr/local/opt/fftw/lib /usr/local/lib)
        if(NOT FFTW3F_LIB)
            message(FATAL_ERROR "${PROJECT_NAME}: FFTW3f not found. brew install fftw, or pass -DFFTW_DIR=<prefix>")
        endif()
        message(WARNING "${PROJECT_NAME}: no static fftw3f; the bundle will depend on the shared library")
    endif()
    find_path(FFTW3F_INC fftw3.h HINTS /opt/homebrew/opt/fftw/include /usr/local/opt/fftw/include /usr/local/include REQUIRED)
endif()
message(STATUS "${PROJECT_NAME}: FFTW3f ${FFTW3F_LIB}")
