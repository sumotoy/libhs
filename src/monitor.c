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
#include "device_priv.h"
#include "monitor_priv.h"

struct hs_monitor {
    _HS_MONITOR
};

struct callback {
    _hs_list_head list;
    int id;

    hs_monitor_callback_func *f;
    void *udata;
};

int hs_monitor_register_callback(hs_monitor *monitor, hs_monitor_callback_func *f, void *udata)
{
    assert(monitor);
    assert(f);

    struct callback *callback = calloc(1, sizeof(*callback));
    if (!callback)
        return hs_error(HS_ERROR_MEMORY, NULL);

    callback->id = monitor->callback_id++;
    callback->f = f;
    callback->udata = udata;

    _hs_list_add_tail(&monitor->callbacks, &callback->list);

    return callback->id;
}

static void drop_callback(struct callback *callback)
{
    _hs_list_remove(&callback->list);
    free(callback);
}

void hs_monitor_deregister_callback(hs_monitor *monitor, int id)
{
    assert(monitor);
    assert(id >= 0);

    _hs_list_foreach(cur, &monitor->callbacks) {
        struct callback *callback = _hs_container_of(cur, struct callback, list);
        if (callback->id == id) {
            drop_callback(callback);
            break;
        }
    }
}

int _hs_monitor_init(hs_monitor *monitor)
{
    int r;

    _hs_list_init(&monitor->callbacks);

    r = _hs_htable_init(&monitor->devices, 64);
    if (r < 0)
        return r;

    return 0;
}

void _hs_monitor_release(hs_monitor *monitor)
{
    _hs_list_foreach(cur, &monitor->callbacks) {
        struct callback *callback = _hs_container_of(cur, struct callback, list);
        free(callback);
    }

    hs_htable_foreach(cur, &monitor->devices) {
        hs_device *dev = _hs_container_of(cur, hs_device, hnode);

        dev->monitor = NULL;
        hs_device_unref(dev);
    }
    _hs_htable_release(&monitor->devices);
}

static int trigger_callbacks(hs_device *dev)
{
    _hs_list_foreach(cur, &dev->monitor->callbacks) {
        struct callback *callback = _hs_container_of(cur, struct callback, list);
        int r;

        r = (*callback->f)(dev, callback->udata);
        if (r < 0)
            return r;
        if (r) {
            _hs_list_remove(&callback->list);
            free(callback);
        }
    }

    return 0;
}

int _hs_monitor_add(hs_monitor *monitor, hs_device *dev)
{
    hs_htable_foreach_hash(cur, &monitor->devices, _hs_htable_hash_str(dev->key)) {
        hs_device *dev2 = _hs_container_of(cur, hs_device, hnode);

        if (strcmp(dev2->key, dev->key) == 0 && dev2->iface == dev->iface)
            return 0;
    }

    dev->monitor = monitor;
    dev->state = HS_DEVICE_STATUS_ONLINE;

    hs_device_ref(dev);
    _hs_htable_add(&monitor->devices, _hs_htable_hash_str(dev->key), &dev->hnode);

    return trigger_callbacks(dev);
}

void _hs_monitor_remove(hs_monitor *monitor, const char *key)
{
    hs_htable_foreach_hash(cur, &monitor->devices, _hs_htable_hash_str(key)) {
        hs_device *dev = _hs_container_of(cur, hs_device, hnode);

        if (strcmp(dev->key, key) == 0) {
            dev->state = HS_DEVICE_STATUS_DISCONNECTED;

            trigger_callbacks(dev);

            _hs_htable_remove(&dev->hnode);
            hs_device_unref(dev);
        }
    }
}

int hs_monitor_enumerate(hs_monitor *monitor, hs_monitor_callback_func *f, void *udata)
{
    assert(monitor);
    assert(f);

    hs_htable_foreach(cur, &monitor->devices) {
        hs_device *dev = _hs_container_of(cur, hs_device, hnode);
        int r;

        r = (*f)(dev, udata);
        if (r)
            return r;
    }

    return 0;
}
