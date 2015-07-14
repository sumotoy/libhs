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

#ifndef HS_COMMON_H
#define HS_COMMON_H

#include <stdint.h>

#define HS_VERSION 900
#define HS_VERSION_STRING "0.9.0"

#ifdef __cplusplus
    #define HS_BEGIN_C extern "C" {
    #define HS_END_C }
#else
    #define HS_BEGIN_C
    #define HS_END_C
#endif

HS_BEGIN_C

#ifdef __GNUC__
    #define HS_PUBLIC __attribute__((__visibility__("default")))

    #ifdef __MINGW_PRINTF_FORMAT
        #define HS_PRINTF_FORMAT(fmt, first) __attribute__((__format__(__MINGW_PRINTF_FORMAT, fmt, first)))
    #else
        #define HS_PRINTF_FORMAT(fmt, first) __attribute__((__format__(__printf__, fmt, first)))
    #endif
#else
    #error "This compiler is not supported"
#endif

#ifdef _WIN32
typedef void *hs_descriptor; // HANDLE
#else
typedef int hs_descriptor;
#endif

typedef enum hs_err {
    HS_ERROR_MEMORY        = -1,
    HS_ERROR_NOT_FOUND     = -2,
    HS_ERROR_ACCESS        = -3,
    HS_ERROR_IO            = -4,
    HS_ERROR_SYSTEM        = -5,
    HS_ERROR_INVALID       = -6
} hs_err;

typedef void hs_error_func(hs_err err, const char *msg, void *udata);

HS_PUBLIC uint32_t hs_version(void);
HS_PUBLIC const char *hs_version_string(void);

HS_PUBLIC void hs_error_redirect(hs_error_func *f, void *udata);
HS_PUBLIC void hs_error_mask(hs_err err);
HS_PUBLIC void hs_error_unmask(void);

HS_PUBLIC int hs_error(hs_err err, const char *fmt, ...) HS_PRINTF_FORMAT(2, 3);

HS_END_C

#endif
