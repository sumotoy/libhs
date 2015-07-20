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
#include <CoreFoundation/CFRunLoop.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include "device_priv.h"
#include "hs/hid.h"
#include "list.h"
#include "hs/platform.h"

// Used for HID devices, see serial_posix.c for serial devices
struct hs_handle {
    _HS_HANDLE

    io_service_t service;
    union {
        IOHIDDeviceRef hid;
    };

    uint8_t *buf;
    size_t size;

    pthread_mutex_t mutex;
    bool mutex_init;
    int pipe[2];
    int thread_ret;

    _hs_list_head reports;
    unsigned int allocated_reports;
    _hs_list_head free_reports;

    pthread_t thread;
    pthread_cond_t cond;
    bool cond_init;

    CFRunLoopRef loop;
    CFRunLoopSourceRef shutdown;
};

static void fire_device_event(hs_handle *h)
{
    char buf = '.';
    write(h->pipe[1], &buf, 1);
}

static void reset_device_event(hs_handle *h)
{
    char buf;
    read(h->pipe[0], &buf, 1);
}

static void hid_removal_callback(void *ctx, IOReturn result, void *sender)
{
    _HS_UNUSED(result);
    _HS_UNUSED(sender);

    hs_handle *h = ctx;

    pthread_mutex_lock(&h->mutex);

    CFRelease(h->hid);
    h->hid = NULL;

    CFRunLoopSourceSignal(h->shutdown);
    h->loop = NULL;

    pthread_mutex_unlock(&h->mutex);

    fire_device_event(h);
}

struct hid_report {
    _hs_list_head list;

    size_t size;
    uint8_t data[];
};

static void hid_report_callback(void *ctx, IOReturn result, void *sender,
                                IOHIDReportType report_type, uint32_t report_id,
                                uint8_t *report_data, CFIndex report_size)
{
    _HS_UNUSED(result);
    _HS_UNUSED(sender);

    if (report_type != kIOHIDReportTypeInput)
        return;

    hs_handle *h = ctx;

    struct hid_report *report;
    bool fire;
    int r;

    pthread_mutex_lock(&h->mutex);

    fire = _hs_list_is_empty(&h->reports);

    report = _hs_list_get_first(&h->free_reports, struct hid_report, list);
    if (!report) {
        if (h->allocated_reports < 64) {
            // Don't forget the potential leading report ID
            report = calloc(1, sizeof(struct hid_report) + h->size + 1);
            if (!report) {
                r = hs_error(HS_ERROR_MEMORY, NULL);
                goto cleanup;
            }
            h->allocated_reports++;
        } else {
            // Drop oldest report, too bad for the user
            report = _hs_list_get_first(&h->reports, struct hid_report, list);
        }
    }
    if (report->list.prev)
        _hs_list_remove(&report->list);

    // You never know, even if h->size is supposed to be the maximum input report size
    if (report_size > (CFIndex)h->size)
        report_size = (CFIndex)h->size;

    if (report_id) {
        report->data[0] = (uint8_t)report_id;
        memcpy(report->data + 1, report_data, report_size);

        report->size = (size_t)report_size + 1;
    } else {
        memcpy(report->data, report_data, report_size);

        report->size = (size_t)report_size;
    }

    _hs_list_add_tail(&h->reports, &report->list);

    r = 0;
cleanup:
    if (r < 0)
        h->thread_ret = r;
    pthread_mutex_unlock(&h->mutex);
    if (fire)
        fire_device_event(h);
}

static void *device_thread(void *ptr)
{
    hs_handle *h = ptr;

    CFRunLoopSourceContext shutdown_ctx = {0};
    int r;

    pthread_mutex_lock(&h->mutex);

    h->loop = CFRunLoopGetCurrent();

    shutdown_ctx.info = h->loop;
    shutdown_ctx.perform = (void (*)(void *))CFRunLoopStop;

    /* close_hid_device() could be called before the loop is running, while this thread is between
       pthread_barrier_wait() and CFRunLoopRun(). That's the purpose of the shutdown source. */
    h->shutdown = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &shutdown_ctx);
    if (!h->shutdown) {
        r = hs_error(HS_ERROR_SYSTEM, "CFRunLoopSourceCreate() failed");
        goto error;
    }

    CFRunLoopAddSource(h->loop, h->shutdown, kCFRunLoopCommonModes);
    IOHIDDeviceScheduleWithRunLoop(h->hid, h->loop, kCFRunLoopCommonModes);

    // This thread is ready, open_hid_device() can carry on
    h->thread_ret = 1;
    pthread_cond_signal(&h->cond);
    pthread_mutex_unlock(&h->mutex);

    CFRunLoopRun();

    if (h->hid)
        IOHIDDeviceUnscheduleFromRunLoop(h->hid, h->loop, kCFRunLoopCommonModes);

    pthread_mutex_lock(&h->mutex);
    h->loop = NULL;
    pthread_mutex_unlock(&h->mutex);

    return NULL;

error:
    h->thread_ret = r;
    pthread_cond_signal(&h->cond);
    pthread_mutex_unlock(&h->mutex);
    return NULL;
}

static bool get_hid_device_property_number(IOHIDDeviceRef dev, CFStringRef prop, CFNumberType type,
                                           void *rn)
{
    CFTypeRef data = IOHIDDeviceGetProperty(dev, prop);
    if (!data || CFGetTypeID(data) != CFNumberGetTypeID())
        return false;

    return CFNumberGetValue(data, type, rn);
}

static int open_hid_device(hs_device *dev, hs_handle **rh)
{
    hs_handle *h;
    kern_return_t kret;
    int r;

    h = calloc(1, sizeof(*h));
    if (!h) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto error;
    }
    h->dev = hs_device_ref(dev);

    h->pipe[0] = -1;
    h->pipe[1] = -1;

    h->service = IORegistryEntryFromPath(kIOMasterPortDefault, dev->path);
    if (!h->service) {
        r = hs_error(HS_ERROR_NOT_FOUND, "Device '%s' not found", dev->path);
        goto error;
    }

    h->hid = IOHIDDeviceCreate(kCFAllocatorDefault, h->service);
    if (!h->hid) {
        r = hs_error(HS_ERROR_NOT_FOUND, "Device '%s' not found", dev->path);
        goto error;
    }

    kret = IOHIDDeviceOpen(h->hid, 0);
    if (kret != kIOReturnSuccess) {
        r = hs_error(HS_ERROR_SYSTEM, "Failed to open HID device '%s'", dev->path);
        goto error;
    }

    r = get_hid_device_property_number(h->hid, CFSTR(kIOHIDMaxInputReportSizeKey), kCFNumberSInt32Type,
                                       &h->size);
    if (!r) {
        r = hs_error(HS_ERROR_SYSTEM, "HID device '%s' has no valid report size key", dev->path);
        goto error;
    }
    h->buf = malloc(h->size);
    if (!h->buf) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto error;
    }

    IOHIDDeviceRegisterRemovalCallback(h->hid, hid_removal_callback, h);
    IOHIDDeviceRegisterInputReportCallback(h->hid, h->buf, (CFIndex)h->size, hid_report_callback, h);

    r = pipe(h->pipe);
    if (r < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "pipe() failed: %s", strerror(errno));
        goto error;
    }
    fcntl(h->pipe[0], F_SETFL, fcntl(h->pipe[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(h->pipe[1], F_SETFL, fcntl(h->pipe[1], F_GETFL, 0) | O_NONBLOCK);

    _hs_list_init(&h->reports);
    _hs_list_init(&h->free_reports);

    r = pthread_mutex_init(&h->mutex, NULL);
    if (r < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "pthread_mutex_init() failed: %s", strerror(r));
        goto error;
    }
    h->mutex_init = true;

    r = pthread_cond_init(&h->cond, NULL);
    if (r < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "pthread_cond_init() failed: %s", strerror(r));
        goto error;
    }
    h->cond_init = true;

    pthread_mutex_lock(&h->mutex);

    r = pthread_create(&h->thread, NULL, device_thread, h);
    if (r) {
        r = hs_error(HS_ERROR_SYSTEM, "pthread_create() failed: %s", strerror(r));
        goto error;
    }

    /* Barriers are great for this, but OSX doesn't have those... And since it's the only place
       we would use them, it's probably not worth it to have a custom implementation. */
    while (!h->thread_ret)
        pthread_cond_wait(&h->cond, &h->mutex);
    r = h->thread_ret;
    h->thread_ret = 0;
    pthread_mutex_unlock(&h->mutex);
    if (r < 0)
        goto error;

    *rh = h;
    return 0;

error:
    hs_handle_close(h);
    return r;
}

static void close_hid_device(hs_handle *h)
{
    if (h) {
        if (h->shutdown) {
            pthread_mutex_lock(&h->mutex);

            if (h->loop) {
                CFRunLoopSourceSignal(h->shutdown);
                CFRunLoopWakeUp(h->loop);
            }

            pthread_mutex_unlock(&h->mutex);
            pthread_join(h->thread, NULL);

            CFRelease(h->shutdown);
        }

        if (h->cond_init)
            pthread_cond_destroy(&h->cond);
        if (h->mutex_init)
            pthread_mutex_destroy(&h->mutex);

        _hs_list_splice(&h->free_reports, &h->reports);
        _hs_list_foreach(cur, &h->free_reports) {
            struct hid_report *report = _hs_container_of(cur, struct hid_report, list);
            free(report);
        }

        close(h->pipe[0]);
        close(h->pipe[1]);

        free(h->buf);

        if (h->hid) {
            IOHIDDeviceClose(h->hid, 0);
            CFRelease(h->hid);
        }
        if (h->service)
            IOObjectRelease(h->service);

        hs_device_unref(h->dev);
    }

    free(h);
}

static hs_descriptor get_hid_descriptor(const hs_handle *h)
{
    return h->pipe[0];
}

const struct _hs_device_vtable _hs_darwin_hid_vtable = {
    .open = open_hid_device,
    .close = close_hid_device,

    .get_descriptor = get_hid_descriptor
};

int hs_hid_parse_descriptor(hs_handle *h, hs_hid_descriptor *desc)
{
    if (!h->hid)
        return hs_error(HS_ERROR_IO, "Device '%s' was removed", h->dev->path);

    memset(desc, 0, sizeof(*desc));

    get_hid_device_property_number(h->hid, CFSTR(kIOHIDPrimaryUsagePageKey), kCFNumberSInt16Type,
                                   &desc->usage_page);
    get_hid_device_property_number(h->hid, CFSTR(kIOHIDPrimaryUsageKey), kCFNumberSInt16Type,
                                   &desc->usage);

    return 0;
}

ssize_t hs_hid_read(hs_handle *h, uint8_t *buf, size_t size, int timeout)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);
    assert(size);

    struct pollfd pfd;
    uint64_t start;
    struct hid_report *report;
    ssize_t r;

    if (!h->hid)
        return hs_error(HS_ERROR_IO, "Device '%s' was removed", h->dev->path);

    pfd.events = POLLIN;
    pfd.fd = h->pipe[0];

    start = hs_millis();
restart:
    r = poll(&pfd, 1, hs_adjust_timeout(timeout, start));
    if (r < 0) {
        if (errno == EINTR)
            goto restart;

        return hs_error(HS_ERROR_SYSTEM, "poll('%s') failed: %s", h->dev->path, strerror(errno));
    }
    if (!r)
        return 0;

    pthread_mutex_lock(&h->mutex);

    if (h->thread_ret < 0) {
        r = h->thread_ret;
        h->thread_ret = 0;

        goto cleanup;
    }

    report = _hs_list_get_first(&h->reports, struct hid_report, list);
    assert(report);

    if (size > report->size)
        size = report->size;

    memcpy(buf, report->data, size);

    _hs_list_remove(&report->list);
    _hs_list_add(&h->free_reports, &report->list);

    r = (ssize_t)size;
cleanup:
    if (_hs_list_is_empty(&h->reports))
        reset_device_event(h);
    pthread_mutex_unlock(&h->mutex);
    return r;
}

static ssize_t send_report(hs_handle *h, IOHIDReportType type, const uint8_t *buf, size_t size)
{
    uint8_t report;
    kern_return_t kret;

    if (!h->hid)
        return hs_error(HS_ERROR_IO, "Device '%s' was removed", h->dev->path);

    if (size < 2)
        return 0;

    report = buf[0];
    if (!report) {
        buf++;
        size--;
    }

    // FIXME: detect various errors, here and elsewhere for common kIOReturn values
    kret = IOHIDDeviceSetReport(h->hid, type, report, buf, (CFIndex)size);
    if (kret != kIOReturnSuccess)
        return hs_error(HS_ERROR_SYSTEM, "IOHIDDeviceSetReport() failed");

    return (ssize_t)size + !report;
}

ssize_t hs_hid_write(hs_handle *h, const uint8_t *buf, size_t size)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);

    return send_report(h, kIOHIDReportTypeOutput, buf, size);
}

ssize_t hs_hid_send_feature_report(hs_handle *h, const uint8_t *buf, size_t size)
{
    assert(h);
    assert(h->dev->type == HS_DEVICE_TYPE_HID);
    assert(buf);

    return send_report(h, kIOHIDReportTypeFeature, buf, size);
}
