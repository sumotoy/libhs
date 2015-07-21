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

#ifndef HS_PLATFORM_H
#define HS_PLATFORM_H

#include "common.h"

HS_BEGIN_C

#ifdef _WIN32
enum {
    HS_WIN32_VERSION_2000 = 500,
    HS_WIN32_VERSION_XP = 501,
    HS_WIN32_VERSION_2003 = 502,
    HS_WIN32_VERSION_VISTA = 600,
    HS_WIN32_VERSION_7 = 601,
    HS_WIN32_VERSION_8 = 602,
    HS_WIN32_VERSION_8_1 = 603,
    HS_WIN32_VERSION_10 = 1000
};
#endif

typedef struct hs_descriptor_set {
    unsigned int count;

    hs_descriptor desc[64];
    int id[64];
} hs_descriptor_set;

HS_PUBLIC uint64_t hs_millis(void);

HS_PUBLIC int hs_adjust_timeout(int timeout, uint64_t start);

HS_PUBLIC void hs_descriptor_set_clear(hs_descriptor_set *set);
HS_PUBLIC void hs_descriptor_set_add(hs_descriptor_set *set, hs_descriptor desc, int id);
HS_PUBLIC void hs_descriptor_set_remove(hs_descriptor_set *set, int id);

HS_PUBLIC int hs_poll(const hs_descriptor_set *set, int timeout);

#ifdef __linux__
HS_PUBLIC uint32_t hs_linux_version(void);
#endif

#ifdef _WIN32
HS_PUBLIC const char *hs_win32_strerror(unsigned long err);
HS_PUBLIC uint32_t hs_win32_version(void);
#endif

HS_END_C

#endif
