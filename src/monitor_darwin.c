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
    io_iterator_t attach_it;
    io_iterator_t detach_it;
    int notify_ret;

    int kqfd;
    mach_port_t port_set;

    _hs_list_head controllers;
};

extern const struct _hs_device_vtable _hs_posix_device_vtable;
extern const struct _hs_device_vtable _hs_darwin_hid_vtable;

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

static int get_object_interface(io_service_t service, CFUUIDRef uuid, IUnknownVTbl **robject)
{
    IOCFPlugInInterface **plugin = NULL;
    int32_t score;
    IUnknownVTbl *object = NULL;
    kern_return_t kret;
    int r;

    kret = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID,
                                             kIOCFPlugInInterfaceID, &plugin,
                                             &score);
    if (kret != kIOReturnSuccess || !plugin) {
        r = 0;
        goto cleanup;
    }

    kret = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(uuid), (void **)&object);
    if (kret != kIOReturnSuccess || !object) {
        r = 0;
        goto cleanup;
    }

    *robject = object;
    object = NULL;

    r = 1;
cleanup:
    if (object)
        object->Release(object);
    if (plugin)
        (*plugin)->Release(plugin);
    return r;
}

static void clear_iterator(io_iterator_t it)
{
    io_object_t object;
    while ((object = IOIteratorNext(it)))
        IOObjectRelease(object);
}

struct iokit_device {
    io_service_t service;
    IOUSBDeviceInterface **iface;
};

static int find_serial_device_node(io_service_t service, char **rpath)
{
    io_service_t stream = 0, client = 0;
    char *path;
    kern_return_t kret;
    int r;

    kret = IORegistryEntryGetChildEntry(service, kIOServicePlane, &stream);
    if (kret != kIOReturnSuccess || !IOObjectConformsTo(stream, "IOSerialStreamSync")) {
        hs_error(HS_ERROR_SYSTEM, "Serial device interface does not have IOSerialStreamSync child");
        r = 0;
        goto cleanup;
    }

    kret = IORegistryEntryGetChildEntry(stream, kIOServicePlane, &client);
    if (kret != kIOReturnSuccess || !IOObjectConformsTo(client, "IOSerialBSDClient")) {
        hs_error(HS_ERROR_SYSTEM, "Serial device interface does not have IOSerialBSDClient child");
        r = 0;
        goto cleanup;
    }

    r = get_ioregistry_value_string(client, CFSTR("IOCalloutDevice"), &path);
    if (r <= 0) {
        if (!r)
            hs_error(HS_ERROR_SYSTEM, "Serial device does not have property IOCalloutDevice");
        goto cleanup;
    }

    *rpath = path;
    r = 1;
cleanup:
    if (client)
        IOObjectRelease(client);
    if (stream)
        IOObjectRelease(stream);
    return r;
}

static int find_hid_device_node(io_service_t service, char **rpath)
{
    io_string_t buf;
    char *path;
    kern_return_t kret;

    kret = IORegistryEntryGetPath(service, kIOServicePlane, buf);
    if (kret != kIOReturnSuccess) {
        hs_error(HS_ERROR_SYSTEM, "IORegistryEntryGetPath() failed");
        return 0;
    }

    path = strdup(buf);
    if (!path)
        return hs_error(HS_ERROR_MEMORY, NULL);

    *rpath = path;
    return 1;
}

static int find_device_node(hs_device *dev, io_service_t service)
{
    io_service_t spec_service;
    kern_return_t kret;
    int r;

    kret = IORegistryEntryGetChildEntry(service, kIOServicePlane, &spec_service);
    if (kret != kIOReturnSuccess)
        return 0;

    if (IOObjectConformsTo(spec_service, "IOSerialDriverSync")) {
        dev->type = HS_DEVICE_TYPE_SERIAL;
        dev->vtable = &_hs_posix_device_vtable;

        r = find_serial_device_node(spec_service, &dev->path);
    } else if (IOObjectConformsTo(spec_service, "IOHIDDevice")) {
        dev->type = HS_DEVICE_TYPE_HID;
        dev->vtable = &_hs_darwin_hid_vtable;

        r = find_hid_device_node(spec_service, &dev->path);
    } else {
        r = 0;
    }

    IOObjectRelease(spec_service);
    return r;
}

struct usb_controller {
    _hs_list_head list;

    uint8_t index;
    uint64_t session;
};

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

static int resolve_device_location(struct iokit_device *iodev, _hs_list_head *controllers,
                                   char **rlocation)
{
    uint8_t ports[16];
    unsigned int depth;
    uint64_t session;
    kern_return_t kret;
    int r;

    r = get_ioregistry_value_number(iodev->service, CFSTR("PortNum"), kCFNumberSInt8Type, &ports[0]);
    if (!r) {
        hs_error(HS_ERROR_SYSTEM, "Missing property 'PortNum' for USB device");
        return 0;
    }
    depth = 1;

    IOObjectRetain(iodev->service);

    io_service_t parent = iodev->service;
    while (depth < _HS_COUNTOF(ports)) {
        io_service_t tmp = parent;

        kret = IORegistryEntryGetParentEntry(tmp, kIOUSBPlane, &parent);
        IOObjectRelease(tmp);
        if (kret != kIOReturnSuccess) {
            hs_error(HS_ERROR_SYSTEM, "IORegistryEntryGetParentEntry() failed");
            return 0;
        }

        r = get_ioregistry_value_number(parent, CFSTR("PortNum"), kCFNumberSInt8Type, &ports[depth]);
        if (!r)
            break;
        depth++;
    }
    if (depth == _HS_COUNTOF(ports)) {
        hs_error(HS_ERROR_SYSTEM, "Excessive USB location depth");
        return 0;
    }

    r = get_ioregistry_value_number(parent, CFSTR("sessionID"), kCFNumberSInt64Type, &session);
    IOObjectRelease(parent);
    if (!r) {
        hs_error(HS_ERROR_SYSTEM, "Missing property 'sessionID' for USB device");
        return 0;
    }

    _hs_list_foreach(cur, controllers) {
        struct usb_controller *controller = _hs_container_of(cur, struct usb_controller, list);

        if (controller->session == session) {
            ports[depth++] = controller->index;
            break;
        }
    }

    for (unsigned int i = 0; i < depth / 2; i++) {
        uint8_t tmp = ports[i];

        ports[i] = ports[depth - i - 1];
        ports[depth - i - 1] = tmp;
    }

    r = build_location_string(ports, depth, rlocation);
    if (r < 0)
        return r;

    return 1;
}

static int make_device_for_interface(hs_monitor *monitor, struct iokit_device *iodev,
                                     io_service_t iface_service)
{
    hs_device *dev;
    uint64_t session;
    int r;

    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }
    dev->refcount = 1;

    r = get_ioregistry_value_number(iodev->service, CFSTR("sessionID"), kCFNumberSInt64Type, &session);
    if (!r) {
        hs_error(HS_ERROR_SYSTEM, "Missing property 'sessionID' for USB device interface");
        goto cleanup;
    }

    r = get_ioregistry_value_number(iface_service, CFSTR("bInterfaceNumber"), kCFNumberSInt8Type,
                                    &dev->iface);
    if (!r) {
        hs_error(HS_ERROR_SYSTEM, "Missing property 'bInterfaceNumber' for USB device interface");
        goto cleanup;
    }

    (*iodev->iface)->GetDeviceVendor(iodev->iface, &dev->vid);
    (*iodev->iface)->GetDeviceProduct(iodev->iface, &dev->pid);

    r = asprintf(&dev->key, "%"PRIx64, session);
    if (r < 0) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto cleanup;
    }

    r = get_ioregistry_value_string(iodev->service, CFSTR("USB Serial Number"), &dev->serial);
    if (r < 0)
        goto cleanup;

    r = resolve_device_location(iodev, &monitor->controllers, &dev->location);
    if (r <= 0)
        goto cleanup;

    r = find_device_node(dev, iface_service);
    if (r <= 0)
        goto cleanup;

    r = _hs_monitor_add(monitor, dev);
cleanup:
    hs_device_unref(dev);
    return r;
}

static int process_darwin_device(hs_monitor *monitor, io_service_t device_service)
{
    io_name_t cls;
    struct iokit_device iodev = {0};
    IOUSBFindInterfaceRequest request;
    io_iterator_t interfaces = 0;
    io_service_t iface;
    kern_return_t kret;
    int r;

    IOObjectGetClass(device_service, cls);
    if (strcmp(cls, "IOUSBDevice") != 0)
        return 0;

    iodev.service = device_service;

    r = get_object_interface(device_service, kIOUSBDeviceInterfaceID, (IUnknownVTbl **)&iodev.iface);
    if (r <= 0)
        goto cleanup;

    request.bInterfaceClass    = kIOUSBFindInterfaceDontCare;
    request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    request.bAlternateSetting  = kIOUSBFindInterfaceDontCare;

    kret = (*iodev.iface)->CreateInterfaceIterator(iodev.iface, &request, &interfaces);
    if (kret != kIOReturnSuccess) {
        hs_error(HS_ERROR_SYSTEM, "IOUSBDevice::CreateInterfaceIterator() failed");
        r = 0;
        goto cleanup;
    }

    while ((iface = IOIteratorNext(interfaces))) {
        r = make_device_for_interface(monitor, &iodev, iface);
        if (r < 0)
            goto cleanup;

        IOObjectRelease(iface);
    }

    r = 1;
cleanup:
    if (interfaces) {
        clear_iterator(interfaces);
        IOObjectRelease(interfaces);
    }
    if (iodev.iface)
        (*iodev.iface)->Release(iodev.iface);
    return r;
}

static int list_devices(hs_monitor *monitor)
{
    io_service_t service;
    int r;

    while ((service = IOIteratorNext(monitor->attach_it))) {
        r = process_darwin_device(monitor, service);
        if (r < 0)
            goto error;

        IOObjectRelease(service);
    }

    return 0;

error:
    clear_iterator(monitor->attach_it);
    return r;
}

static void darwin_devices_attached(void *ptr, io_iterator_t devices)
{
    // devices == h->attach_t
    _HS_UNUSED(devices);

    hs_monitor *monitor = ptr;

    int r;

    r = list_devices(monitor);
    if (r < 0)
        monitor->notify_ret = r;
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

static void darwin_devices_detached(void *ptr, io_iterator_t devices)
{
    hs_monitor *monitor = ptr;

    io_service_t service;
    while ((service = IOIteratorNext(devices))) {
        remove_device(monitor, service);
        IOObjectRelease(service);
    }
}

static int add_controller(hs_monitor *monitor, uint8_t i, io_service_t service)
{
    struct usb_controller *controller;
    int r;

    controller = calloc(1, sizeof(*controller));
    if (!controller) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto error;
    }

    controller->index = i;
    r = get_ioregistry_value_number(service, CFSTR("sessionID"), kCFNumberSInt64Type,
                                    &controller->session);
    if (!r)
        goto error;

    _hs_list_add(&monitor->controllers, &controller->list);

    return 0;

error:
    free(controller);
    return r;
}

static int list_controllers(hs_monitor *monitor)
{
    io_iterator_t controllers = 0;
    io_service_t service;
    kern_return_t kret;
    int r;

    kret = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching("IOUSBRootHubDevice"),
                                        &controllers);
    if (kret != kIOReturnSuccess) {
        r = hs_error(HS_ERROR_SYSTEM, "IOServiceGetMatchingServices() failed");
        goto cleanup;
    }

    uint8_t i = 0;
    while ((service = IOIteratorNext(controllers))) {
        r = add_controller(monitor, ++i, service);
        if (r < 0)
            goto cleanup;
        IOObjectRelease(service);
    }

    r = 0;
cleanup:
    if (controllers) {
        clear_iterator(controllers);
        IOObjectRelease(controllers);
    }
    return r;
}

int hs_monitor_new(hs_monitor **rmonitor)
{
    assert(rmonitor);

    hs_monitor *monitor;
    struct kevent kev;
    const struct timespec ts = {0};
    kern_return_t kret;
    int r;

    monitor = calloc(1, sizeof(*monitor));
    if (!monitor) {
        r = hs_error(HS_ERROR_MEMORY, NULL);
        goto error;
    }
    monitor->kqfd = -1;

    _hs_list_init(&monitor->controllers);

    monitor->notify_port = IONotificationPortCreate(kIOMasterPortDefault);
    if (!monitor->notify_port) {
        r = hs_error(HS_ERROR_SYSTEM, "IONotificationPortCreate() failed");
        goto error;
    }

    kret = IOServiceAddMatchingNotification(monitor->notify_port, kIOFirstMatchNotification,
                                            IOServiceMatching(kIOUSBDeviceClassName),
                                            darwin_devices_attached,
                                            monitor, &monitor->attach_it);
    if  (kret != kIOReturnSuccess) {
        r = hs_error(HS_ERROR_SYSTEM, "IOServiceAddMatchingNotification() failed");
        goto error;
    }

    kret = IOServiceAddMatchingNotification(monitor->notify_port, kIOTerminatedNotification,
                                            IOServiceMatching(kIOUSBDeviceClassName),
                                            darwin_devices_detached,
                                            monitor, &monitor->detach_it);
    if  (kret != kIOReturnSuccess) {
        r = hs_error(HS_ERROR_SYSTEM, "IOServiceAddMatchingNotification() failed");
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

    r = list_controllers(monitor);
    if (r < 0)
        goto error;

    r = list_devices(monitor);
    if (r < 0)
        goto error;
    clear_iterator(monitor->detach_it);

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

        _hs_list_foreach(cur, &monitor->controllers) {
            struct usb_controller *controller = _hs_container_of(cur, struct usb_controller, list);
            free(controller);
        }

        close(monitor->kqfd);
        if (monitor->port_set)
            mach_port_deallocate(mach_task_self(), monitor->port_set);

        // I don't know how these functions are supposed to treat NULL
        if (monitor->attach_it)
            IOObjectRelease(monitor->attach_it);
        if (monitor->detach_it)
            IOObjectRelease(monitor->detach_it);
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

        if (monitor->notify_ret < 0) {
            r = monitor->notify_ret;
            monitor->notify_ret = 0;

            break;
        }
    }

    return r;
}
