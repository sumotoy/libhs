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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "device_win32_priv.h"
#include "hs/hid.h"
#include "hs/platform.h"

#ifdef __MINGW32__
// MinGW may miss these
__declspec(dllimport) BOOLEAN NTAPI HidD_GetPreparsedData(HANDLE HidDeviceObject, PHIDP_PREPARSED_DATA *PreparsedData);
__declspec(dllimport) BOOLEAN NTAPI HidD_FreePreparsedData(PHIDP_PREPARSED_DATA PreparsedData);
#endif

int hs_hid_parse_descriptor(hs_handle *h, hs_hid_descriptor *rdesc)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(rdesc);

    // semi-hidden Hungarian pointers? Really , Microsoft?
    PHIDP_PREPARSED_DATA pp;
    HIDP_CAPS caps;
    LONG ret;

    ret = HidD_GetPreparsedData(h->handle, &pp);
    if (!ret)
        return hs_error(HS_ERROR_SYSTEM, "HidD_GetPreparsedData() failed");

    // NTSTATUS and BOOL are both defined as LONG
    ret = HidP_GetCaps(pp, &caps);
    HidD_FreePreparsedData(pp);
    if (ret != HIDP_STATUS_SUCCESS)
        return hs_error(HS_ERROR_INVALID, "Invalid HID descriptor");

    rdesc->usage = caps.Usage;
    rdesc->usage_page = caps.UsagePage;

    return 0;
}

ssize_t hs_hid_read(hs_handle *h, uint8_t *buf, size_t size, int timeout)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);
    assert(size);

    if (h->len < 0) {
        // Could be a transient error, try to restart it
        h->len = _hs_win32_start_async_read(h);
        if (h->len < 0)
            return h->len;
    }

    h->len = _hs_win32_finalize_async_read(h, timeout);
    if (h->len <= 0)
        return h->len;

    /* HID communication is message-based. So if the caller does not provide a big enough
       buffer, we can just discard the extra data, unlike for serial communication. */
    if (h->len) {
        if (h->buf[0]) {
            if (size > (size_t)h->len)
                size = (size_t)h->len;
            memcpy(buf, h->buf, size);
        } else {
            if (size > (size_t)--h->len)
                size = (size_t)h->len;
            memcpy(buf, h->buf + 1, size);
        }
    } else {
        size = 0;
    }

    hs_error_mask(HS_ERROR_IO);
    h->len = _hs_win32_start_async_read(h);
    hs_error_unmask();

    return (ssize_t)size;
}

ssize_t hs_hid_write(hs_handle *h, const uint8_t *buf, size_t size)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);

    if (size < 2)
        return 0;

    OVERLAPPED ov = {0};
    DWORD len;
    BOOL success;

    success = WriteFile(h->handle, buf, (DWORD)size, &len, &ov);
    if (!success) {
        if (GetLastError() != ERROR_IO_PENDING) {
            CancelIo(h->handle);
            return hs_error(HS_ERROR_IO, "I/O error while writing to '%s'", h->dev->path);
        }

        success = GetOverlappedResult(h->handle, &ov, &len, TRUE);
        if (!success)
            return hs_error(HS_ERROR_IO, "I/O error while writing to '%s'", h->dev->path);
    }

    return (ssize_t)len;
}

ssize_t hs_hid_send_feature_report(hs_handle *h, const uint8_t *buf, size_t size)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);

    if (size < 2)
        return 0;

    // Timeout behavior?
    BOOL success = HidD_SetFeature(h->handle, (char *)buf, (DWORD)size);
    if (!success)
        return hs_error(HS_ERROR_IO, "I/O error while writing to '%s'", h->dev->path);

    return (ssize_t)size;
}
