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

#ifndef HS_MONITOR_H
#define HS_MONITOR_H

#include "common.h"

HS_BEGIN_C

struct hs_device;

typedef struct hs_monitor hs_monitor;

typedef enum hs_monitor_event {
    HS_MONITOR_EVENT_ADDED,
    HS_MONITOR_EVENT_REMOVED
} hs_monitor_event;

typedef int hs_monitor_callback_func(hs_monitor_event event, struct hs_device *dev, void *udata);

HS_PUBLIC int hs_monitor_new(hs_monitor **rmonitor);
HS_PUBLIC void hs_monitor_free(hs_monitor *monitor);

HS_PUBLIC int hs_monitor_register_callback(hs_monitor *monitor, hs_monitor_callback_func *f, void *udata);
HS_PUBLIC void hs_monitor_deregister_callback(hs_monitor *monitor, int id);

HS_PUBLIC hs_descriptor hs_monitor_get_descriptor(const hs_monitor *monitor);
HS_PUBLIC int hs_monitor_refresh(hs_monitor *monitor);

HS_PUBLIC int hs_monitor_enumerate(hs_monitor *monitor, hs_monitor_callback_func *f, void *udata);

HS_END_C

#endif
