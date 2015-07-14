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
#include <linux/hidraw.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "device_posix_priv.h"
#include "hs/hid.h"
#include "hs/platform.h"

static int parse_descriptor(struct hidraw_report_descriptor *report, hs_hid_descriptor *desc)
{
    size_t size;
    for (size_t i = 0; i < report->size; i += size + 1) {
        unsigned int type;
        uint32_t data;

        type = report->value[i] & 0xFC;
        size = report->value[i] & 3;
        if (size == 3)
            size = 4;

        if (i + size >= report->size)
            break;

        // Little Endian
        switch (size) {
        case 0:
            data = 0;
            break;
        case 1:
            data = report->value[i + 1];
            break;
        case 2:
            data = (uint32_t)(report->value[i + 2] << 8) | report->value[i + 1];
            break;
        case 4:
            data = (uint32_t)(report->value[i + 4] << 24) | (uint32_t)(report->value[i + 3] << 16)
                | (uint32_t)(report->value[i + 2] << 8) | report->value[i + 1];
            break;

        // WTF?
        default:
            return hs_error(HS_ERROR_INVALID, "Invalid HID descriptor");
        }

        switch (type) {
        case 0x04:
            desc->usage_page = (uint16_t)data;
            break;
        case 0x08:
            desc->usage = (uint16_t)data;
            break;

        // Collection
        case 0xA0:
            return 0;
        }
    }

    return 0;
}

int hs_hid_parse_descriptor(hs_handle *h, hs_hid_descriptor *desc)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(desc);

    struct hidraw_report_descriptor report;
    int size, r;

    r = ioctl(h->fd, HIDIOCGRDESCSIZE, &size);
    if (r < 0)
        return hs_error(HS_ERROR_SYSTEM, "ioctl('%s', HIDIOCGRDESCSIZE) failed: %s",
                        h->dev->path, strerror(errno));

    report.size = (uint32_t)size;
    r = ioctl(h->fd, HIDIOCGRDESC, &report);
    if (r < 0)
        return hs_error(HS_ERROR_SYSTEM, "ioctl('%s', HIDIOCGRDESC) failed: %s", h->dev->path,
                        strerror(errno));

    memset(desc, 0, sizeof(*desc));

    return parse_descriptor(&report, desc);
}

ssize_t hs_hid_read(hs_handle *h, uint8_t *buf, size_t size, int timeout)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);
    assert(size);

    ssize_t r;

    if (timeout) {
        struct pollfd pfd;
        uint64_t start;

        pfd.events = POLLIN;
        pfd.fd = h->fd;

        start = hs_millis();
restart:
        r = poll(&pfd, 1, hs_adjust_timeout(timeout, start));
        if (r < 0) {
            if (errno == EINTR)
                goto restart;
            return hs_error(HS_ERROR_SYSTEM, "poll('%s') failed: %s", h->dev->path,
                            strerror(errno));
        }
        if (!r)
            return 0;
    }

    r = read(h->fd, buf, size);
    if (r < 0) {
        switch (errno) {
        case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return 0;
        case EIO:
        case ENXIO:
            return hs_error(HS_ERROR_IO, "I/O error while reading from '%s'", h->dev->path);
        }
        return hs_error(HS_ERROR_SYSTEM, "read('%s') failed: %s", h->dev->path, strerror(errno));
    }

    return r;
}

ssize_t hs_hid_write(hs_handle *h, const uint8_t *buf, size_t size)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);

    if (size < 2)
        return 0;

    ssize_t r;

restart:
    // On linux, USB requests timeout after 5000ms and O_NONBLOCK isn't honoured for write
    r = write(h->fd, (const char *)buf, size);
    if (r < 0) {
        switch (errno) {
        case EINTR:
            goto restart;
        case EIO:
        case ENXIO:
            return hs_error(HS_ERROR_IO, "I/O error while writing to '%s'", h->dev->path);
        }
        return hs_error(HS_ERROR_SYSTEM, "write('%s') failed: %s", h->dev->path, strerror(errno));
    }

    return r;
}

ssize_t hs_hid_send_feature_report(hs_handle *h, const uint8_t *buf, size_t size)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);

    if (size < 2)
        return 0;

    int r;

restart:
    r = ioctl(h->fd, HIDIOCSFEATURE(size), (const char *)buf);
    if (r < 0) {
        switch (errno) {
        case EINTR:
            goto restart;
        case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return 0;
        case EIO:
        case ENXIO:
            return hs_error(HS_ERROR_IO, "I/O error while writing to '%s'", h->dev->path);
        }
        return hs_error(HS_ERROR_SYSTEM, "ioctl('%s', HIDIOCSFEATURE) failed: %s", h->dev->path,
                        strerror(errno));
    }

    return (ssize_t)size;
}
