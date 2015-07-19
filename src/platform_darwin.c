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
#include <mach/mach_time.h>
#include <sys/select.h>
#include "hs/platform.h"

uint64_t hs_millis(void)
{
    static mach_timebase_info_data_t tb;
    if (!tb.numer)
        mach_timebase_info(&tb);

    return (uint64_t)mach_absolute_time() * tb.numer / tb.denom / 1000000;
}

int hs_poll(const hs_descriptor_set *set, int timeout)
{
    assert(set);
    assert(set->count);
    assert(set->count <= 64);

    fd_set fds;
    uint64_t start;
    int r;

    FD_ZERO(&fds);
    for (unsigned int i = 0; i < set->count; i++)
        FD_SET(set->desc[i], &fds);

    start = hs_millis();
restart:
    if (timeout >= 0) {
        int adjusted_timeout;
        struct timeval tv;

        adjusted_timeout = hs_adjust_timeout(timeout, start);
        tv.tv_sec = adjusted_timeout / 1000;
        tv.tv_usec = (adjusted_timeout % 1000) * 1000;

        r = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
    } else {
        r = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    }
    if (r < 0) {
        if (errno == EINTR)
            goto restart;
        return hs_error(HS_ERROR_SYSTEM, "poll() failed: %s", strerror(errno));
    }
    if (!r)
        return 0;

    for (unsigned int i = 0; i < set->count; i++) {
        if (FD_ISSET(set->desc[i], &fds))
            return set->id[i];
    }
    assert(false);
}
