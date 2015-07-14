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
#include "hs/platform.h"

int hs_adjust_timeout(int timeout, uint64_t start)
{
    if (timeout < 0)
        return -1;

    uint64_t now = hs_millis();

    if (now > start + (uint64_t)timeout)
        return 0;
    return (int)(start + (uint64_t)timeout - now);
}

void hs_descriptor_set_clear(hs_descriptor_set *set)
{
    assert(set);

    set->count = 0;
}

void hs_descriptor_set_add(hs_descriptor_set *set, hs_descriptor desc, int id)
{
    assert(set);
    assert(set->count < _HS_COUNTOF(set->desc));
#ifdef _WIN32
    assert(desc);
#else
    assert(desc >= 0);
#endif

    set->desc[set->count] = desc;
    set->id[set->count] = id;

    set->count++;
}

void hs_descriptor_set_remove(hs_descriptor_set *set, int id)
{
    assert(set);

    unsigned int count = 0;
    for (unsigned int i = 0; i < set->count; i++) {
        if (set->id[i] != id) {
            set->desc[count] = set->desc[i];
            set->id[count] = set->id[i];

            count++;
        }
    }

    set->count = count;
}
