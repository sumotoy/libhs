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
#include <fcntl.h>
#include <libudev.h>
#include <pthread.h>
#include <unistd.h>
#include "device_priv.h"
#include "monitor_priv.h"
#include "hs/platform.h"

struct hs_monitor {
    _HS_MONITOR

    struct udev_monitor *monitor;
};

struct udev_aggregate {
    struct udev_device *dev;
    struct udev_device *usb;
    struct udev_device *iface;
};

extern const struct _hs_device_vtable _hs_posix_device_vtable;
extern const struct _hs_device_vtable _hs_linux_hid_vtable;

static const char *device_subsystems[] = {
    "input",
    "hidraw",
    "tty",
    NULL
};

static pthread_mutex_t udev_lock = PTHREAD_MUTEX_INITIALIZER;
static struct udev *udev;

_HS_EXIT()
{
    udev_unref(udev);
    pthread_mutex_destroy(&udev_lock);
}

static int compute_device_location(struct udev_device *dev, char **rlocation)
{
    const char *busnum, *devpath;
    char *location;
    int r;

    busnum = udev_device_get_sysattr_value(dev, "busnum");
    devpath = udev_device_get_sysattr_value(dev, "devpath");

    if (!busnum || !devpath)
        return 0;

    r = asprintf(&location, "usb-%s-%s", busnum, devpath);
    if (r < 0)
        return hs_error(HS_ERROR_MEMORY, NULL);

    for (char *ptr = location; *ptr; ptr++) {
        if (*ptr == '.')
            *ptr = '-';
    }

    *rlocation = location;
    return 1;
}

static int fill_device_details(hs_device *dev, struct udev_aggregate *agg)
{
    const char *buf;
    int r;

    buf = udev_device_get_subsystem(agg->dev);
    if (!buf)
        return 0;

    if (strcmp(buf, "hidraw") == 0) {
        dev->type = HS_DEVICE_TYPE_HID;
        dev->vtable = &_hs_linux_hid_vtable;
    } else if (strcmp(buf, "tty") == 0) {
        dev->type = HS_DEVICE_TYPE_SERIAL;
        dev->vtable = &_hs_posix_device_vtable;
    } else {
        return 0;
    }

    buf = udev_device_get_devnode(agg->dev);
    if (!buf || access(buf, F_OK) != 0)
        return 0;
    dev->path = strdup(buf);
    if (!dev->path)
        return hs_error(HS_ERROR_MEMORY, NULL);

    dev->key = strdup(udev_device_get_devpath(agg->dev));
    if (!dev->key)
        return hs_error(HS_ERROR_MEMORY, NULL);

    r = compute_device_location(agg->usb, &dev->location);
    if (r <= 0)
        return r;

    errno = 0;
    buf = udev_device_get_sysattr_value(agg->usb, "idVendor");
    if (!buf)
        return 0;
    dev->vid = (uint16_t)strtoul(buf, NULL, 16);
    if (errno)
        return 0;

    errno = 0;
    buf = udev_device_get_sysattr_value(agg->usb, "idProduct");
    if (!buf)
        return 0;
    dev->pid = (uint16_t)strtoul(buf, NULL, 16);
    if (errno)
        return 0;

    buf = udev_device_get_sysattr_value(agg->usb, "manufacturer");
    if (buf) {
        dev->manufacturer = strdup(buf);
        if (!dev->manufacturer)
            return hs_error(HS_ERROR_MEMORY, NULL);
    }

    buf = udev_device_get_sysattr_value(agg->usb, "product");
    if (buf) {
        dev->product = strdup(buf);
        if (!dev->product)
            return hs_error(HS_ERROR_MEMORY, NULL);
    }

    buf = udev_device_get_sysattr_value(agg->usb, "serial");
    if (buf) {
        dev->serial = strdup(buf);
        if (!dev->serial)
            return hs_error(HS_ERROR_MEMORY, NULL);
    }

    errno = 0;
    buf = udev_device_get_devpath(agg->iface);
    buf += strlen(buf) - 1;
    dev->iface = (uint8_t)strtoul(buf, NULL, 10);
    if (errno)
        return 0;

    return 1;
}

static int read_device_information(struct udev_device *udev_dev, hs_device **rdev)
{
    struct udev_aggregate agg;
    hs_device *dev = NULL;
    int r;

    agg.dev = udev_dev;
    agg.usb = udev_device_get_parent_with_subsystem_devtype(agg.dev, "usb", "usb_device");
    agg.iface = udev_device_get_parent_with_subsystem_devtype(agg.dev, "usb", "usb_interface");
    if (!agg.usb || !agg.iface) {
        r = 0;
        goto cleanup;
    }

    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }
    dev->refcount = 1;

    r = fill_device_details(dev, &agg);
    if (r <= 0)
        goto cleanup;

    *rdev = dev;
    dev = NULL;

    r = 1;
cleanup:
    hs_device_unref(dev);
    return r;
}

static int init_udev(void)
{
    if (udev)
        return 0;

    pthread_mutex_lock(&udev_lock);
    if (!udev)
        udev = udev_new();
    pthread_mutex_unlock(&udev_lock);
    if (!udev)
        return hs_error(HS_ERROR_SYSTEM, "udev_new() failed");

    return 0;
}

int hs_enumerate(hs_monitor_callback_func *f, void *udata)
{
    assert(f);

    struct udev_enumerate *enumerate = NULL;
    int r;

    r = init_udev();
    if (r < 0)
        goto cleanup;

    enumerate = udev_enumerate_new(udev);
    if (!enumerate) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }

    udev_enumerate_add_match_is_initialized(enumerate);
    for (const char **cur = device_subsystems; *cur; cur++) {
        r = udev_enumerate_add_match_subsystem(enumerate, *cur);
        if (r < 0) {
            r = hs_error(HS_ERROR_MEMORY, NULL);
            goto cleanup;
        }
    }

    // Current implementation of udev_enumerate_scan_devices() does not fail
    r = udev_enumerate_scan_devices(enumerate);
    if (r < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "udev_enumerate_scan_devices() failed");
        goto cleanup;
    }

    struct udev_list_entry *cur;
    udev_list_entry_foreach(cur, udev_enumerate_get_list_entry(enumerate)) {
        struct udev_device *udev_dev;
        hs_device *dev;

        udev_dev = udev_device_new_from_syspath(udev, udev_list_entry_get_name(cur));
        if (!udev_dev) {
            if (errno == ENOMEM) {
                r = hs_error(HS_ERROR_MEMORY, NULL);
                goto cleanup;
            }
            continue;
        }

        r = read_device_information(udev_dev, &dev);
        udev_device_unref(udev_dev);
        if (r < 0)
            goto cleanup;
        if (!r)
            continue;

        r = (*f)(dev, udata);
        hs_device_unref(dev);
        if (r)
            goto cleanup;
    }

    r = 0;
cleanup:
    udev_enumerate_unref(enumerate);
    return r;
}

static int monitor_enumerate_callback(hs_device *dev, void *udata)
{
    return _hs_monitor_add(udata, dev);
}

int hs_monitor_new(hs_monitor **rmonitor)
{
    assert(rmonitor);

    hs_monitor *monitor = NULL;
    int r;

    r = init_udev();
    if (r < 0)
        goto error;

    monitor = calloc(1, sizeof(*monitor));
    if (!monitor) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto error;
    }

    monitor->monitor = udev_monitor_new_from_netlink(udev, "udev");
    if (!monitor->monitor) {
        r = hs_error(HS_ERROR_SYSTEM, "udev_monitor_new_from_netlink() failed");
        goto error;
    }

    for (const char **cur = device_subsystems; *cur; cur++) {
        r = udev_monitor_filter_add_match_subsystem_devtype(monitor->monitor, *cur, NULL);
        if (r < 0) {
            r = hs_error(HS_ERROR_SYSTEM, "udev_monitor_filter_add_match_subsystem_devtype() failed");
            goto error;
        }
    }

    r = udev_monitor_enable_receiving(monitor->monitor);
    if (r < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "udev_monitor_enable_receiving() failed");
        goto error;
    }

    r = _hs_monitor_init(monitor);
    if (r < 0)
        goto error;

    r = hs_enumerate(monitor_enumerate_callback, monitor);
    if (r < 0)
        goto error;

    *rmonitor = monitor;
    return 0;

error:
    hs_monitor_free(monitor);
    return r;
}

void hs_monitor_free(hs_monitor *monitor)
{
    if (monitor) {
        _hs_monitor_release(monitor);
        udev_monitor_unref(monitor->monitor);
    }

    free(monitor);
}

hs_descriptor hs_monitor_get_descriptor(const hs_monitor *monitor)
{
    assert(monitor);
    return udev_monitor_get_fd(monitor->monitor);
}

int hs_monitor_refresh(hs_monitor *monitor)
{
    assert(monitor);

    struct udev_device *udev_dev;
    int r;

    errno = 0;
    while ((udev_dev = udev_monitor_receive_device(monitor->monitor))) {
        const char *action = udev_device_get_action(udev_dev);

        r = 0;
        if (strcmp(action, "add") == 0) {
            hs_device *dev = NULL;

            r = read_device_information(udev_dev, &dev);
            if (r > 0)
                r = _hs_monitor_add(monitor, dev);

            hs_device_unref(dev);
        } else if (strcmp(action, "remove") == 0) {
            _hs_monitor_remove(monitor, udev_device_get_devpath(udev_dev));
        }

        udev_device_unref(udev_dev);

        if (r < 0)
            return r;

        errno = 0;
    }
    if (errno == ENOMEM)
        return hs_error(HS_ERROR_MEMORY, NULL);

    return 0;
}
