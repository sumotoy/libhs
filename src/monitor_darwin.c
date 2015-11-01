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
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include "device_priv.h"
#include "list.h"
#include "monitor_priv.h"
#include "hs/platform.h"

struct hs_monitor {
    _HS_MONITOR

    IONotificationPortRef notify_port;
    io_iterator_t iterators[8];
    unsigned int iterator_count;
    int notify_ret;

    int kqfd;
    mach_port_t port_set;
};

struct device_class {
    const char *old_stack;
    const char *new_stack;
};

struct usb_controller {
    _hs_list_head list;

    uint8_t index;
    io_string_t path;
};

extern const struct _hs_device_vtable _hs_posix_device_vtable;
extern const struct _hs_device_vtable _hs_darwin_hid_vtable;

static struct device_class device_classes[] = {
    {"IOHIDDevice", "IOUSBHostHIDDevice"},
    {"IOSerialBSDClient", "IOSerialBSDClient"},
    {NULL}
};

static pthread_mutex_t controllers_lock = PTHREAD_MUTEX_INITIALIZER;
static _HS_LIST(controllers);

_HS_EXIT()
{
    _hs_list_foreach(cur, &controllers) {
        struct usb_controller *controller = _hs_container_of(cur, struct usb_controller, list);
        free(controller);
    }
    pthread_mutex_destroy(&controllers_lock);
}

static bool uses_new_stack()
{
    static bool init, new_stack;

    if (!init) {
        new_stack = hs_darwin_version() >= 150000;
        init = true;
    }

    return new_stack;
}

static const char *correct_class(const char *new_stack, const char *old_stack)
{
    return uses_new_stack() ? new_stack : old_stack;
}

static int get_ioregistry_value_string(io_service_t service, CFStringRef prop, char **rs)
{
    CFTypeRef data;
    CFIndex size;
    char *s;
    int r;

    data = IORegistryEntryCreateCFProperty(service, prop, kCFAllocatorDefault, 0);
    if (!data || CFGetTypeID(data) != CFStringGetTypeID()) {
        r = 0;
        goto cleanup;
    }

    size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(data), kCFStringEncodingUTF8) + 1;

    s = malloc((size_t)size);
    if (!s) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }

    r = CFStringGetCString(data, s, size, kCFStringEncodingUTF8);
    if (!r) {
        r = 0;
        goto cleanup;
    }

    *rs = s;
    r = 1;
cleanup:
    if (data)
        CFRelease(data);
    return r;
}

static ssize_t get_ioregistry_value_data(io_service_t service, CFStringRef prop, uint8_t *buf, size_t size)
{
    CFTypeRef data;
    ssize_t r;

    data = IORegistryEntryCreateCFProperty(service, prop, kCFAllocatorDefault, 0);
    if (!data || CFGetTypeID(data) != CFDataGetTypeID()) {
        r = 0;
        goto cleanup;
    }

    r = (ssize_t)CFDataGetLength(data);
    if (r > (ssize_t)size)
        r = (ssize_t)size;
    CFDataGetBytes(data, CFRangeMake(0, (CFIndex)r), buf);

cleanup:
    if (data)
        CFRelease(data);
    return r;
}

static bool get_ioregistry_value_number(io_service_t service, CFStringRef prop, CFNumberType type,
                                        void *rn)
{
    CFTypeRef data;
    bool r;

    data = IORegistryEntryCreateCFProperty(service, prop, kCFAllocatorDefault, 0);
    if (!data || CFGetTypeID(data) != CFNumberGetTypeID()) {
        r = false;
        goto cleanup;
    }

    r = CFNumberGetValue(data, type, rn);
cleanup:
    if (data)
        CFRelease(data);
    return r;
}

static int get_ioregistry_entry_path(io_service_t service, char **rpath)
{
    io_string_t buf;
    char *path;
    kern_return_t kret;

    kret = IORegistryEntryGetPath(service, kIOServicePlane, buf);
    if (kret != kIOReturnSuccess)
        return hs_error(HS_ERROR_SYSTEM, "IORegistryEntryGetPath() failed");

    path = strdup(buf);
    if (!path)
        return hs_error(HS_ERROR_MEMORY, NULL);

    *rpath = path;
    return 0;
}

static void clear_iterator(io_iterator_t it)
{
    io_object_t object;
    while ((object = IOIteratorNext(it)))
        IOObjectRelease(object);
}

static int find_device_node(hs_device *dev, io_service_t service)
{
    int r;

    if (IOObjectConformsTo(service, "IOSerialBSDClient")) {
        dev->type = HS_DEVICE_TYPE_SERIAL;
        dev->vtable = &_hs_posix_device_vtable;

        r = get_ioregistry_value_string(service, CFSTR("IOCalloutDevice"), &dev->path);
        if (!r)
            hs_log(HS_LOG_WARNING, "Serial device does not have property 'IOCalloutDevice'");
    } else if (IOObjectConformsTo(service, "IOHIDDevice")) {
        dev->type = HS_DEVICE_TYPE_HID;
        dev->vtable = &_hs_darwin_hid_vtable;

        r = get_ioregistry_entry_path(service, &dev->path);
        if (!r)
            r = 1;
    } else {
        hs_log(HS_LOG_WARNING, "Cannot find device node for unknown device entry class");
        r = 0;
    }

    return r;
}

static int build_location_string(uint8_t ports[], unsigned int depth, char **rpath)
{
    char buf[256];
    char *ptr;
    size_t size;
    char *path;
    int r;

    ptr = buf;
    size = sizeof(buf);

    strcpy(buf, "usb");
    ptr += strlen(buf);
    size -= (size_t)(ptr - buf);

    for (unsigned int i = 0; i < depth; i++) {
        r = snprintf(ptr, size, "-%hhu", ports[i]);
        assert(r >= 2 && (size_t)r < size);

        ptr += r;
        size -= (size_t)r;
    }

    path = strdup(buf);
    if (!path)
        return hs_error(HS_ERROR_MEMORY, NULL);

    *rpath = path;
    return 0;
}

static uint8_t find_controller(io_service_t service)
{
    io_string_t path;
    kern_return_t kret;
    uint8_t index;

    kret = IORegistryEntryGetPath(service, correct_class(kIOServicePlane, kIOUSBPlane), path);
    if (kret != kIOReturnSuccess)
        return 0;

    index = 0;
    _hs_list_foreach(cur, &controllers) {
        struct usb_controller *controller = _hs_container_of(cur, struct usb_controller, list);

        if (strcmp(controller->path, path) == 0) {
            index = controller->index;
            break;
        }
    }

    return index;
}

static io_service_t get_parent_and_release(io_service_t service, const io_name_t plane)
{
    io_service_t parent;
    kern_return_t kret;

    kret = IORegistryEntryGetParentEntry(service, plane, &parent);
    IOObjectRelease(service);
    if (kret != kIOReturnSuccess)
        return 0;

    return parent;
}

static int resolve_device_location(io_service_t service, char **rlocation)
{
    uint8_t ports[16];
    unsigned int depth = 0;
    int r;

    IOObjectRetain(service);

    do {
        if (uses_new_stack()) {
            depth += !!get_ioregistry_value_data(service, CFSTR("port"), &ports[depth], sizeof(ports[depth]));
        } else {
            depth += get_ioregistry_value_number(service, CFSTR("PortNum"), kCFNumberSInt8Type, &ports[depth]);
        }

        if (depth == _HS_COUNTOF(ports)) {
            hs_log(HS_LOG_WARNING, "Excessive USB location depth, ignoring device");
            r = 0;
            goto cleanup;
        }

        service = get_parent_and_release(service, correct_class(kIOServicePlane, kIOUSBPlane));
    } while (service && !IOObjectConformsTo(service, correct_class("AppleUSBHostController", "IOUSBRootHubDevice")));

    if (!depth) {
        hs_log(HS_LOG_WARNING, "Failed to build USB device location string, ignoring");
        r = 0;
        goto cleanup;
    }

    ports[depth] = find_controller(service);
    if (!ports[depth]) {
        hs_log(HS_LOG_WARNING, "Cannot find matching USB Host controller, ignoring device");
        r = 0;
        goto cleanup;
    }
    depth++;

    for (unsigned int i = 0; i < depth / 2; i++) {
        uint8_t tmp = ports[i];

        ports[i] = ports[depth - i - 1];
        ports[depth - i - 1] = tmp;
    }

    r = build_location_string(ports, depth, rlocation);
    if (r < 0)
        goto cleanup;

    r = 1;
cleanup:
    if (service)
        IOObjectRelease(service);
    return r;
}

static io_service_t find_conforming_parent(io_service_t service, const char *cls)
{
    IOObjectRetain(service);
    do {
        service = get_parent_and_release(service, kIOServicePlane);
    } while (service && !IOObjectConformsTo(service, cls));

    return service;
}

static int process_darwin_device(io_service_t service, hs_device **rdev)
{
    io_service_t dev_service = 0, iface_service = 0;
    hs_device *dev = NULL;
    uint64_t session;
    int r;

    iface_service = find_conforming_parent(service, "IOUSBInterface");
    if (!iface_service) {
        r = 0;
        goto cleanup;
    }
    dev_service = find_conforming_parent(iface_service, "IOUSBDevice");
    if (!dev_service) {
        r = 0;
        goto cleanup;
    }

    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }
    dev->refcount = 1;

#define GET_PROPERTY_NUMBER(service, key, type, var) \
        r = get_ioregistry_value_number(service, CFSTR(key), type, var); \
        if (!r) { \
            hs_log(HS_LOG_WARNING, "Missing property '%s', ignoring device", key); \
            goto cleanup; \
        }

    GET_PROPERTY_NUMBER(dev_service, "sessionID", kCFNumberSInt64Type, &session);
    GET_PROPERTY_NUMBER(dev_service, "idVendor", kCFNumberSInt64Type, &dev->vid);
    GET_PROPERTY_NUMBER(dev_service, "idProduct", kCFNumberSInt64Type, &dev->pid);
    GET_PROPERTY_NUMBER(iface_service, "bInterfaceNumber", kCFNumberSInt64Type, &dev->iface);

#undef GET_PROPERTY_NUMBER

    r = asprintf(&dev->key, "%"PRIx64, session);
    if (r < 0) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }

#define GET_PROPERTY_STRING(service, key, var) \
        r = get_ioregistry_value_string((service), CFSTR(key), (var)); \
        if (r < 0) \
            goto cleanup;

    GET_PROPERTY_STRING(dev_service, "USB Vendor Name", &dev->manufacturer);
    GET_PROPERTY_STRING(dev_service, "USB Product Name", &dev->product);
    GET_PROPERTY_STRING(dev_service, "USB Serial Number", &dev->serial);

#undef GET_PROPERTY_STRING

    r = resolve_device_location(dev_service, &dev->location);
    if (r <= 0)
        goto cleanup;

    r = find_device_node(dev, service);
    if (r <= 0)
        goto cleanup;

    *rdev = dev;
    dev = NULL;
    r = 1;

cleanup:
    hs_device_unref(dev);
    if (dev_service)
        IOObjectRelease(dev_service);
    if (iface_service)
        IOObjectRelease(iface_service);
    return r;
}

static int process_iterator_devices(io_iterator_t it, hs_monitor_callback_func *f, void *udata)
{
    io_service_t service;

    while ((service = IOIteratorNext(it))) {
        hs_device *dev;
        int r;

        r = process_darwin_device(service, &dev);
        IOObjectRelease(service);
        if (r < 0)
            return r;
        if (!r)
            continue;

        r = (*f)(dev, udata);
        hs_device_unref(dev);
        if (r)
            return r;
    }

    return 0;
}

static int monitor_enumerate_callback(hs_device *dev, void *udata)
{
    return _hs_monitor_add(udata, dev);
}

static void darwin_devices_attached(void *ptr, io_iterator_t it)
{
    hs_monitor *monitor = ptr;

    monitor->notify_ret = process_iterator_devices(it, monitor_enumerate_callback, monitor);
}

static void remove_device(hs_monitor *monitor, io_service_t device_service)
{
    uint64_t session;
    char key[16];
    int r;

    r = get_ioregistry_value_number(device_service, CFSTR("sessionID"), kCFNumberSInt64Type, &session);
    if (!r)
        return;

    snprintf(key, sizeof(key), "%"PRIx64, session);
    _hs_monitor_remove(monitor, key);
}

static void darwin_devices_detached(void *ptr, io_iterator_t it)
{
    hs_monitor *monitor = ptr;

    io_service_t service;
    while ((service = IOIteratorNext(it))) {
        remove_device(monitor, service);
        IOObjectRelease(service);
    }
}

static int add_controller(io_service_t service, uint8_t index)
{
    io_name_t path;
    struct usb_controller *controller;
    kern_return_t kret;

    kret = IORegistryEntryGetPath(service, correct_class(kIOServicePlane, kIOUSBPlane), path);
    if (kret != kIOReturnSuccess) {
        hs_log(HS_LOG_WARNING, "IORegistryEntryGetPath() failed: %d", kret);
        return 0;
    }

    controller = calloc(1, sizeof(*controller));
    if (!controller)
        return hs_error(HS_ERROR_MEMORY, NULL);

    controller->index = index;
    strcpy(controller->path, path);

    _hs_list_add(&controllers, &controller->list);

    return 1;
}

static int populate_controllers(void)
{
    io_iterator_t it = 0;
    io_service_t service;
    kern_return_t kret;
    int r;

    pthread_mutex_lock(&controllers_lock);

    if (!_hs_list_is_empty(&controllers)) {
        r = 0;
        goto cleanup;
    }

    kret = IOServiceGetMatchingServices(kIOMasterPortDefault,
                                        IOServiceMatching(correct_class("AppleUSBHostController", "IOUSBRootHubDevice")),
                                        &it);
    if (kret != kIOReturnSuccess) {
        r = hs_error(HS_ERROR_SYSTEM, "IOServiceGetMatchingServices() failed");
        goto cleanup;
    }

    uint8_t index = 0;
    while ((service = IOIteratorNext(it))) {
        if (index == UINT8_MAX) {
            hs_log(HS_LOG_WARNING, "Reached maximum controller ID %d, ignoring", UINT8_MAX);
            break;
        }

        r = add_controller(service, ++index);
        IOObjectRelease(service);
        if (r < 0)
            goto cleanup;
    }

    r = 0;
cleanup:
    pthread_mutex_unlock(&controllers_lock);
    if (it) {
        clear_iterator(it);
        IOObjectRelease(it);
    }
    return r;
}

int hs_enumerate(hs_monitor_callback_func *f, void *udata)
{
    assert(f);

    io_iterator_t it = 0;
    kern_return_t kret;
    int r;

    r = populate_controllers();
    if (r < 0)
        goto cleanup;

    for (unsigned int i = 0; device_classes[i].old_stack; i++) {
        const char *cls = correct_class(device_classes[i].new_stack, device_classes[i].old_stack);

        kret = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(cls), &it);
        if (kret != kIOReturnSuccess) {
            r = hs_error(HS_ERROR_SYSTEM, "IOServiceGetMatchingServices('%s') failed", cls);
            goto cleanup;
        }

        r = process_iterator_devices(it, f, udata);
        if (r)
            goto cleanup;

        IOObjectRelease(it);
        it = 0;
    }

    r = 0;
cleanup:
    if (it) {
        clear_iterator(it);
        IOObjectRelease(it);
    }
    return r;
}

static int add_notification(hs_monitor *monitor, const char *cls, const io_name_t type,
                            IOServiceMatchingCallback f, io_iterator_t *rit)
{
    io_iterator_t it;
    kern_return_t kret;

    kret = IOServiceAddMatchingNotification(monitor->notify_port, type, IOServiceMatching(cls),
                                            f, monitor, &it);
    if (kret != kIOReturnSuccess)
        return hs_error(HS_ERROR_SYSTEM, "IOServiceAddMatchingNotification('%s') failed", cls);

    assert(monitor->iterator_count < _HS_COUNTOF(monitor->iterators));
    monitor->iterators[monitor->iterator_count++] = it;
    *rit = it;

    return 0;
}

int hs_monitor_new(hs_monitor **rmonitor)
{
    assert(rmonitor);

    hs_monitor *monitor = NULL;
    struct kevent kev;
    const struct timespec ts = {0};
    io_iterator_t it;
    kern_return_t kret;
    int r;

    r = populate_controllers();
    if (r < 0)
        goto error;

    monitor = calloc(1, sizeof(*monitor));
    if (!monitor) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto error;
    }
    monitor->kqfd = -1;

    monitor->notify_port = IONotificationPortCreate(kIOMasterPortDefault);
    if (!monitor->notify_port) {
        r = hs_error(HS_ERROR_SYSTEM, "IONotificationPortCreate() failed");
        goto error;
    }

    monitor->kqfd = kqueue();
    if (monitor->kqfd < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "kqueue() failed: %s", strerror(errno));
        goto error;
    }

    kret = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &monitor->port_set);
    if (kret != KERN_SUCCESS) {
        r = hs_error(HS_ERROR_SYSTEM, "mach_port_allocate() failed");
        goto error;
    }

    kret = mach_port_insert_member(mach_task_self(), IONotificationPortGetMachPort(monitor->notify_port),
                                   monitor->port_set);
    if (kret != KERN_SUCCESS) {
        r = hs_error(HS_ERROR_SYSTEM, "mach_port_insert_member() failed");
        goto error;
    }

    EV_SET(&kev, monitor->port_set, EVFILT_MACHPORT, EV_ADD, 0, 0, NULL);

    r = kevent(monitor->kqfd, &kev, 1, NULL, 0, &ts);
    if (r < 0) {
        r = hs_error(HS_ERROR_SYSTEM, "kevent() failed: %d", errno);
        goto error;
    }

    r = _hs_monitor_init(monitor);
    if (r < 0)
        goto error;

    for (unsigned int i = 0; device_classes[i].old_stack; i++) {
        r = add_notification(monitor, correct_class(device_classes[i].new_stack, device_classes[i].old_stack),
                             kIOFirstMatchNotification, darwin_devices_attached, &it);
        if (r < 0)
            goto error;

        r = process_iterator_devices(it, monitor_enumerate_callback, monitor);
        if (r < 0)
            goto error;
    }

    r = add_notification(monitor, correct_class("IOUSBHostDevice", kIOUSBDeviceClassName),
                         kIOTerminatedNotification, darwin_devices_detached, &it);
    if (r < 0)
        goto error;
    clear_iterator(it);

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

        close(monitor->kqfd);
        if (monitor->port_set)
            mach_port_deallocate(mach_task_self(), monitor->port_set);

        for (unsigned int i = 0; i < monitor->iterator_count; i++) {
            clear_iterator(monitor->iterators[i]);
            IOObjectRelease(monitor->iterators[i]);
        }

        if (monitor->notify_port)
            IONotificationPortDestroy(monitor->notify_port);
    }

    free(monitor);
}

hs_descriptor hs_monitor_get_descriptor(const hs_monitor *monitor)
{
    assert(monitor);
    return monitor->kqfd;
}

int hs_monitor_refresh(hs_monitor *monitor)
{
    assert(monitor);

    struct kevent kev;
    const struct timespec ts = {0};
    int r;

    r = kevent(monitor->kqfd, NULL, 0, &kev, 1, &ts);
    if (r < 0)
        return hs_error(HS_ERROR_SYSTEM, "kevent() failed: %s", strerror(errno));
    if (!r)
        return 0;
    assert(kev.filter == EVFILT_MACHPORT);

    r = 0;
    while (true) {
        struct {
            mach_msg_header_t header;
            uint8_t body[128];
        } msg;
        mach_msg_return_t mret;

        mret = mach_msg(&msg.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(msg),
                        monitor->port_set, 0, MACH_PORT_NULL);
        if (mret != MACH_MSG_SUCCESS) {
            if (mret == MACH_RCV_TIMED_OUT)
                break;

            r = hs_error(HS_ERROR_SYSTEM, "mach_msg() failed");
            break;
        }

        IODispatchCalloutFromMessage(NULL, &msg.header, monitor->notify_port);

        if (monitor->notify_ret) {
            r = monitor->notify_ret;
            monitor->notify_ret = 0;

            break;
        }
    }

    return r;
}
