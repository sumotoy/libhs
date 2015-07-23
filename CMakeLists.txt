# The MIT License (MIT)
#
# Copyright (c) 2015 Niels Martignène <niels.martignene@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

cmake_minimum_required(VERSION 2.8.12)
project(libhs C)

option(BUILD_SHARED_LIBS "Build libhs with shared libraries." ON)

if(CMAKE_C_COMPILER_ID STREQUAL Clang)
    set(CMAKE_COMPILER_IS_CLANG 1)
endif()

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG)
    set(CMAKE_C_FLAGS "-std=gnu99 -fvisibility=hidden -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wno-missing-field-initializers -Wshadow -Wconversion -Wformat=2")

    set(CMAKE_C_FLAGS_DEBUG "-O1 -g -D_FORTIFY_SOURCE=2")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -D_FORTIFY_SOURCE=2")
    set(CMAKE_C_FLAGS_RELEASE "-O2 -D_FORTIFY_SOURCE=2")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(LINUX 1)
endif()

if(LINUX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUDEV REQUIRED libudev)

    include_directories(${LIBUDEV_INCLUDE_DIRS})
    list(APPEND HS_LINK_LIBRARIES ${LIBUDEV_LIBRARIES})
endif()

if(APPLE)
    find_library(COREFOUNDATION_LIBRARIES CoreFoundation)
    find_library(IOKIT_LIBRARIES IOKit)
    list(APPEND HS_LINK_LIBRARIES ${COREFOUNDATION_LIBRARIES} ${IOKIT_LIBRARIES})

    if(POLICY CMP0042)
        cmake_policy(SET CMP0042 NEW)
    endif()

    set(CMAKE_INSTALL_NAME_DIR "@rpath")
    set(CMAKE_INSTALL_RPATH "@executable_path;@executable_path/../lib;@executable_path/../Frameworks")

    # Makes it easier for cross-compilation and testing. In my case, compile on Linux
    # and test on OSX using sshfs
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

if(WIN32)
    add_definitions(-DWINVER=0x0602 -D_WIN32_WINNT=0x0602)
    list(APPEND HS_LINK_LIBRARIES setupapi hid)

    if(MINGW)
        set(CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES} -static-libgcc")
        set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -static-libgcc")
    endif()
endif()

include(CheckSymbolExists)
check_symbol_exists(stpcpy string.h HAVE_STPCPY)
check_symbol_exists(asprintf stdio.h HAVE_ASPRINTF)
add_definitions(-DHAVE_CONFIG_H)
configure_file(src/config.h.in config.h)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(include)

set(HS_SOURCES include/hs.h
               include/hs/common.h
               include/hs/device.h
               include/hs/hid.h
               include/hs/monitor.h
               include/hs/platform.h
               include/hs/serial.h

               src/common.c
               src/compat.c
               src/device.c
               src/device_priv.h
               src/htable.c
               src/list.h
               src/monitor.c
               src/monitor_priv.h
               src/platform.c
               src/util.h)
if(WIN32)
    list(APPEND HS_SOURCES src/device_win32.c
                           src/device_win32_priv.h
                           src/hid_win32.c
                           src/monitor_win32.c
                           src/platform_win32.c
                           src/serial_win32.c)
else()
    list(APPEND HS_SOURCES src/device_posix.c
                           src/device_posix_priv.h
                           src/serial_posix.c)

    if(LINUX)
        list(APPEND HS_SOURCES src/hid_linux.c
                               src/monitor_linux.c
                               src/platform_posix.c)
    elseif(APPLE)
        list(APPEND HS_SOURCES src/hid_darwin.c
                               src/monitor_darwin.c
                               src/platform_darwin.c)
    else()
        message(FATAL_ERROR "Unsupported platform")
    endif()
endif()

add_library(hs ${HS_SOURCES})
target_link_libraries(hs ${HS_LINK_LIBRARIES})
