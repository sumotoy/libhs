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

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif
#include "hs.h"

static int device_callback(hs_device *dev, void *udata)
{
    (void)(udata);

    int event_char;
    const char *type;

    /* Use hs_device_get_status() to differenciate between added and removed devices,
       when called from hs_monitor_enumerate() it is always HS_DEVICE_STATUS_ONLINE. */
    switch (hs_device_get_status(dev)) {
    case HS_DEVICE_STATUS_DISCONNECTED:
        event_char = '-';
        break;
    case HS_DEVICE_STATUS_ONLINE:
        event_char = '+';
        break;
    }

    switch (hs_device_get_type(dev)) {
    case HS_DEVICE_TYPE_HID:
        type = "hid";
        break;
    case HS_DEVICE_TYPE_SERIAL:
        type = "serial";
        break;
    }

    printf("%c %s %04"PRIx16":%04"PRIx16" (%s)\n  @ %s\n", event_char, hs_device_get_location(dev),
           hs_device_get_vid(dev), hs_device_get_pid(dev), type, hs_device_get_path(dev));

    /* If you return a non-zero value, the enumeration/refresh is aborted and this value
       is returned from the calling function. */
    return 0;
}

int main(void)
{
    hs_monitor *monitor = NULL;
    /* This is a quick way to clear the descriptor set, you are free to use `set->count = 0;`
       or hs_descriptor_set_clear(). */
    hs_descriptor_set set = {0};
    int r;

    r = hs_monitor_new(&monitor);
    if (r < 0)
        goto cleanup;

    printf("Current devices:\n");

    /* hs_monitor_new() goes through the device tree and makes an initial device list, so we
       don't need hs_monitor_refresh() yet. But if you don't call hs_monitor_enumerate()
       immediately, you need to use hs_monitor_refresh() before hs_monitor_enumerate(). */
    r = hs_monitor_enumerate(monitor, device_callback, NULL);
    if (r < 0)
        goto cleanup;

    /* Register our event callback, this is called from within hs_monitor_refresh() whenever
       something interesting happens (i.e. a device is added or removed). */
    r = hs_monitor_register_callback(monitor, device_callback, NULL);
    if (r < 0)
        goto cleanup;
    printf("\n");

    /* Add the waitable descriptor provided by the monitor to the descriptor set, it will
       become ready when there are pending events. */
    hs_descriptor_set_add(&set, hs_monitor_get_descriptor(monitor), 1);
    /* We also want to poll the terminal/console input buffer. */
#ifdef _WIN32
    hs_descriptor_set_add(&set, GetStdHandle(STD_INPUT_HANDLE), 2);
#else
    hs_descriptor_set_add(&set, STDIN_FILENO, 2);
#endif

    printf("Monitoring devices (press RETURN to end):\n");
    do {
        /* This function is non-blocking, if there are no pending events it does nothing and
           returns immediately. */
        r = hs_monitor_refresh(monitor);
        if (r < 0)
            goto cleanup;

        /* This function returns the value associated with the ready descriptor, in this case
           1 when there are monitor events and 2 when RETURN is pressed. */
        r = hs_poll(&set, -1);
    } while (r == 1);

    /* Clear the terminal input buffer, just to avoid the extra return/characters from
     * showing up when this program exits. This has nothing to do with libhs. */
    if (r == 2) {
        char buf[32];

        read(STDIN_FILENO, buf, sizeof(buf));
        r = 0;
    }

cleanup:
    hs_monitor_free(monitor);
    return -r;
}
