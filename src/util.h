/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Niels Martignène <niels.martignene@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _HS_UTIL_H
#define _HS_UTIL_H

// Disable MSVC's stupid deprecation warnings
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE

// Avoid msvcrt's limited versions of printf/scanf functions
#define __USE_MINGW_ANSI_STDIO 1

#include "hs/common.h"
#include "compat.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
    #define _HS_INIT() \
        __attribute__((constructor)) \
        static void _HS_UNIQUE_ID(init_)(void)
    #define _HS_EXIT() \
        __attribute__((destructor)) \
        static void _HS_UNIQUE_ID(exit_)(void)
#elif defined(_MSC_VER)
    #define _HS_INIT() \
        static void __cdecl _HS_UNIQUE_ID(init_)(void); \
        __pragma(section(".CRT$XCU", read)) \
        __declspec(allocate(".CRT$XCU")) void (__cdecl* _HS_UNIQUE_ID(init_) ## _)(void) = _HS_UNIQUE_ID(init_); \
        static void __cdecl _HS_UNIQUE_ID(init_)(void)
    #define _HS_EXIT() \
        static void __cdecl _HS_UNIQUE_ID(exit_)(void); \
        _HS_INIT() \
        { \
            atexit(_HS_UNIQUE_ID(exit_)); \
        } \
        static void __cdecl _HS_UNIQUE_ID(exit_)(void)
#endif

#define _HS_UNUSED(arg) ((void)(arg))

#define _HS_COUNTOF(a) (sizeof(a) / sizeof(*(a)))

#define _HS_CONCAT_HELPER(a, b) a ## b
#define _HS_CONCAT(a, b) _HS_CONCAT_HELPER(a, b)

#define _HS_UNIQUE_ID(prefix) _HS_CONCAT(prefix, __LINE__)

#define _hs_container_of(head, type, member) \
    ((type *)((char *)(head) - (size_t)(&((type *)0)->member)))

#endif
