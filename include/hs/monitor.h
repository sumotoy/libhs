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

/**
 * @defgroup monitor Device discovery
 */

struct hs_device;

/**
 * @ingroup monitor
 * @brief Opaque structure representing a device monitor.
 */
typedef struct hs_monitor hs_monitor;

/**
 * @ingroup monitor
 * @brief Device enumeration and event callback.
 *
 * When refreshing, use hs_device_get_status() to distinguish between added and removed events.
 *
 * You must return 0 to continue the enumeration or event processing. Non-zero values stop the
 * process and are returned from the enumeration/refresh function. You should probably use
 * negative values for errors (@ref hs_err) and positive values otherwise, but this is not
 * enforced.
 *
 * @param dev   Device object.
 * @param udata Pointer to user-defined arbitrary data.
 *
 * @return Return 0 to continue the enumeration/event processing, or any other value to abort.
 *     The enumeration/refresh function will then return this value unchanged.
 *
 * @sa hs_monitor_enumerate() for enumeration.
 * @sa hs_monitor_refresh() for event processing.
 */
typedef int hs_monitor_callback_func(struct hs_device *dev, void *udata);

/**
 * @ingroup monitor
 * @brief Open a new device monitor.
 *
 * @param[out] rmonitor A pointer to the variable that receives the device monitor, it will stay
 *     unchanged if the function fails.
 * @return This function returns 0 on success, or a negative @ref hs_err code otherwise.
 *
 * @sa hs_monitor_free()
 */
HS_PUBLIC int hs_monitor_new(hs_monitor **rmonitor);
/**
 * @ingroup monitor
 * @brief Close a device monitor.
 *
 * You should not keep any device object or handles beyond this call. In practice, call this at
 * the end of your program.
 *
 * @param monitor Device monitor.
 *
 * @sa hs_monitor_new()
 */
HS_PUBLIC void hs_monitor_free(hs_monitor *monitor);

/**
 * @ingroup monitor
 * @brief Get a pollable descriptor for device monitor events.
 *
 * @ref hs_descriptor is a typedef to the platform descriptor type: int on POSIX platforms,
 * HANDLE on Windows.
 *
 * You can use this descriptor with select()/poll() on POSIX platforms and the Wait
 * (e.g. WaitForSingleObject()) functions on Windows to know when there are pending device events.
 * Cross-platform facilities are provided to ease this, see @ref hs_descriptor_set.
 *
 * Call hs_monitor_refresh() to process events.
 *
 * @param monitor Device monitor.
 * @return This function returns a pollable descriptor, call hs_monitor_refresh() when it
 *     becomes ready.
 *
 * @sa hs_descriptor
 * @sa hs_monitor_refresh()
 */
HS_PUBLIC hs_descriptor hs_monitor_get_descriptor(const hs_monitor *monitor);

/**
 * @ingroup monitor
 * @brief Register a device event callback.
 *
 * This callback will be called from hs_monitor_refresh() when a new device is detected or
 * a device is removed. In the callback, use hs_device_get_status() to distinguish the two.
 * See hs_monitor_callback_func() for more information.
 *
 * You can unregister callbacks using hs_monitor_deregister_callback(), use the callback ID
 * returned by this function.
 *
 * @param monitor Device monitor.
 * @param f       Device event callback.
 * @param udata   Pointer to user-defined arbitrary data for the callback.
 * @return This function returns the callback ID on success, or a negative @ref hs_err code.
 *
 * @sa hs_monitor_callback_func()
 * @sa hs_monitor_deregister_callback()
 * @sa hs_monitor_refresh()
 */
HS_PUBLIC int hs_monitor_register_callback(hs_monitor *monitor, hs_monitor_callback_func *f, void *udata);
/**
 * @ingroup monitor
 * @brief Deregister a device event callback.
 *
 * @param monitor Device monitor.
 * @param id      Callback ID, returned by hs_monitor_register_callback().
 *
 * @sa hs_monitor_register_callback()
 * @sa hs_monitor_refresh()
 */
HS_PUBLIC void hs_monitor_deregister_callback(hs_monitor *monitor, int id);

/**
 * @ingroup monitor
 * @brief Refresh the device list and fire device change events.
 *
 * Process all the pending device change events to refresh the device list
 * and call the revelant callbacks in the process.
 *
 * This function is non-blocking.
 *
 * @param monitor Device monitor.
 * @return This function returns 0 on success, or a negative @ref hs_err code. When a callback
 *     returns a non-zero value, the refresh is interrupted and the value is returned.
 *
 * @sa hs_monitor_callback_func()
 * @sa hs_monitor_register_callback()
 */
HS_PUBLIC int hs_monitor_refresh(hs_monitor *monitor);

/**
 * @ingroup monitor
 * @brief Enumerate the currently known devices.
 *
 * The device list is refreshed when the monitor is created, and when hs_monitor_refresh() is
 * called. This function simply uses the monitor's internal device list.
 * See hs_monitor_callback_func() for more information about the callback.
 *
 * @param monitor Device monitor.
 * @param f       Device enumeration callback.
 * @param udata   Pointer to user-defined arbitrary data for the callback.
 * @return This function returns 0 on success, or a negative @ref hs_err code. When the callback
 *     returns a non-zero value, the enumeration is interrupted and the value is returned.
 *
 * @sa hs_monitor_callback_func()
 */
HS_PUBLIC int hs_monitor_enumerate(hs_monitor *monitor, hs_monitor_callback_func *f, void *udata);

HS_END_C

#endif
