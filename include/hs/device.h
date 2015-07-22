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

#ifndef HS_DEVICE_H
#define HS_DEVICE_H

#include "common.h"

HS_BEGIN_C

struct hs_monitor;

typedef struct hs_device hs_device;
typedef struct hs_handle hs_handle;

typedef enum hs_device_status {
    /** Device has been disconnected. */
    HS_DEVICE_STATUS_DISCONNECTED,
    /** Device is connected and ready. */
    HS_DEVICE_STATUS_ONLINE
} hs_device_status;

typedef enum hs_device_type {
    HS_DEVICE_TYPE_HID,
    HS_DEVICE_TYPE_SERIAL
} hs_device_type;

HS_PUBLIC hs_device *hs_device_ref(hs_device *dev);
HS_PUBLIC void hs_device_unref(hs_device *dev);

HS_PUBLIC hs_device_status hs_device_get_status(const hs_device *dev);
HS_PUBLIC hs_device_type hs_device_get_type(const hs_device *dev);
HS_PUBLIC const char *hs_device_get_location(const hs_device *dev);
HS_PUBLIC uint8_t hs_device_get_interface_number(const hs_device *dev);
HS_PUBLIC const char *hs_device_get_path(const hs_device *dev);
HS_PUBLIC uint16_t hs_device_get_vid(const hs_device *dev);
HS_PUBLIC uint16_t hs_device_get_pid(const hs_device *dev);
HS_PUBLIC const char *hs_device_get_serial_number_string(const hs_device *dev);
HS_PUBLIC struct hs_monitor *hs_device_get_monitor(const hs_device *dev);

HS_PUBLIC int hs_device_open(hs_device *dev, hs_handle **rh);

HS_PUBLIC void hs_handle_close(hs_handle *h);

HS_PUBLIC hs_device *hs_handle_get_device(const hs_handle *h);
HS_PUBLIC hs_descriptor hs_handle_get_descriptor(const hs_handle *h);

HS_END_C

#endif
