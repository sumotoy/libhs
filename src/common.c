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

#include "util.h"
#include <stdarg.h>

static void default_handler(hs_err err, const char *msg, void *udata);

static hs_error_func *handler = default_handler;
static void *handler_udata = NULL;

static hs_err mask[32];
static unsigned int mask_count = 0;

uint32_t hs_version(void)
{
    return HS_VERSION;
}

const char *hs_version_string(void)
{
    return HS_VERSION_STRING;
}

static const char *generic_message(int err)
{
    if (err >= 0)
        return "Success";

    switch ((hs_err)err) {
    case HS_ERROR_MEMORY:
        return "Memory error";
    case HS_ERROR_NOT_FOUND:
        return "Not found";
    case HS_ERROR_ACCESS:
        return "Permission error";
    case HS_ERROR_IO:
        return "I/O error";
    case HS_ERROR_SYSTEM:
        return "System error";
    case HS_ERROR_INVALID:
        return "Invalid data error";
    }

    return "Unknown error";
}

static void default_handler(hs_err err, const char *msg, void *udata)
{
    _HS_UNUSED(err);
    _HS_UNUSED(udata);

    fputs(msg, stderr);
    fputc('\n', stderr);
}

void hs_error_redirect(hs_error_func *f, void *udata)
{
    if (f) {
        handler = f;
        handler_udata = udata;
    } else {
        handler = default_handler;
        handler_udata = NULL;
    }
}

void hs_error_mask(hs_err err)
{
    assert(mask_count < _HS_COUNTOF(mask));

    mask[mask_count++] = err;
}

void hs_error_unmask(void)
{
    assert(mask_count);

    mask_count--;
}

int hs_error(hs_err err, const char *fmt, ...)
{
    va_list ap;
    char buf[512];

    va_start(ap, fmt);

    for (unsigned int i = 0; i < mask_count; i++) {
        if (mask[i] == err)
            goto cleanup;
    }

    if (fmt) {
        vsnprintf(buf, sizeof(buf), fmt, ap);
    } else {
        strncpy(buf, generic_message(err), sizeof(buf));
        buf[sizeof(buf) - 1] = 0;
    }

    (*handler)(err, buf, handler_udata);

cleanup:
    va_end(ap);
    return err;
}
