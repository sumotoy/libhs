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

#ifndef HS_HID_H
#define HS_HID_H

#include "common.h"

HS_BEGIN_C

struct hs_handle;

typedef struct hs_hid_descriptor {
    uint16_t usage_page;
    uint16_t usage;
} hs_hid_descriptor;

HS_PUBLIC int hs_hid_parse_descriptor(struct hs_handle *h, hs_hid_descriptor *desc);

HS_PUBLIC ssize_t hs_hid_read(struct hs_handle *h, uint8_t *buf, size_t size, int timeout);
HS_PUBLIC ssize_t hs_hid_write(struct hs_handle *h, const uint8_t *buf, size_t size);

HS_PUBLIC ssize_t hs_hid_get_feature_report(hs_handle *h, uint8_t report_id, uint8_t *buf, size_t size);
HS_PUBLIC ssize_t hs_hid_send_feature_report(struct hs_handle *h, const uint8_t *buf, size_t size);

HS_END_C

#endif
