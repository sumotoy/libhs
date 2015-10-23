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

static void default_handler(hs_log_level level, const char *msg, void *udata);

static hs_log_func *handler = default_handler;
static void *handler_udata = NULL;

static hs_error_code mask[32];
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

    switch ((hs_error_code)err) {
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

static void default_handler(hs_log_level level, const char *msg, void *udata)
{
    _HS_UNUSED(level);
    _HS_UNUSED(udata);

    if (level == HS_LOG_DEBUG && !getenv("LIBHS_DEBUG"))
        return;

    fputs(msg, stderr);
    fputc('\n', stderr);
}

void hs_log_redirect(hs_log_func *f, void *udata)
{
    if (f) {
        handler = f;
        handler_udata = udata;
    } else {
        handler = default_handler;
        handler_udata = NULL;
    }
}

void hs_error_mask(hs_error_code err)
{
    assert(mask_count < _HS_COUNTOF(mask));

    mask[mask_count++] = err;
}

void hs_error_unmask(void)
{
    assert(mask_count);

    mask_count--;
}

HS_PRINTF_FORMAT(2, 0)
static void logv(hs_log_level level, const char *fmt, va_list ap)
{
    char buf[512];

    vsnprintf(buf, sizeof(buf), fmt, ap);
    (*handler)(level, buf, handler_udata);
}

void hs_log(hs_log_level level, const char *fmt, ...)
{
    assert(fmt);

    va_list ap;

    va_start(ap, fmt);
    logv(level, fmt, ap);
    va_end(ap);
}

int hs_error(hs_error_code err, const char *fmt, ...)
{
    va_list ap;

    for (unsigned int i = 0; i < mask_count; i++) {
        if (mask[i] == err)
            return err;
    }

    if (fmt) {
        va_start(ap, fmt);
        logv(HS_LOG_ERROR, fmt, ap);
        va_end(ap);
    } else {
        hs_log(HS_LOG_ERROR, "%s", generic_message(err));
    }

    return err;
}
