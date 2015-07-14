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

#ifndef HS_SERIAL_H
#define HS_SERIAL_H

#include "common.h"

HS_BEGIN_C

struct hs_handle;

enum {
    HS_SERIAL_CSIZE_MASK   = 0x3,
    HS_SERIAL_7BITS_CSIZE  = 0x1,
    HS_SERIAL_6BITS_CSIZE  = 0x2,
    HS_SERIAL_5BITS_CSIZE  = 0x3,

    HS_SERIAL_PARITY_MASK  = 0xC,
    HS_SERIAL_ODD_PARITY   = 0x4,
    HS_SERIAL_EVEN_PARITY  = 0x8,

    HS_SERIAL_STOP_MASK    = 0x10,
    HS_SERIAL_2BITS_STOP   = 0x10,

    HS_SERIAL_FLOW_MASK    = 0x60,
    HS_SERIAL_XONXOFF_FLOW = 0x20,
    HS_SERIAL_RTSCTS_FLOW  = 0x40,

    HS_SERIAL_CLOSE_MASK   = 0x80,
    HS_SERIAL_NOHUP_CLOSE  = 0x80,
};

HS_PUBLIC int hs_serial_set_attributes(struct hs_handle *h, uint32_t rate, int flags);

HS_PUBLIC ssize_t hs_serial_read(struct hs_handle *h, uint8_t *buf, size_t size, int timeout);
HS_PUBLIC ssize_t hs_serial_write(struct hs_handle *h, const uint8_t *buf, ssize_t size);

HS_END_C

#endif
