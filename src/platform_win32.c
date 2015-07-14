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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "hs/platform.h"

typedef ULONGLONG WINAPI GetTickCount64_func(void);

static ULONGLONG WINAPI GetTickCount64_fallback(void);

static GetTickCount64_func *GetTickCount64_;

_HS_INIT()
{
    HANDLE h = GetModuleHandle("kernel32.dll");
    assert(h);

    GetTickCount64_ = (GetTickCount64_func *)GetProcAddress(h, "GetTickCount64");
    if (!GetTickCount64_)
        GetTickCount64_ = GetTickCount64_fallback;
}

static ULONGLONG WINAPI GetTickCount64_fallback(void)
{
    static LARGE_INTEGER freq;

    LARGE_INTEGER now;
    BOOL ret;

    if (!freq.QuadPart) {
        ret = QueryPerformanceFrequency(&freq);
        assert(ret);
    }

    ret = QueryPerformanceCounter(&now);
    assert(ret);

    return (ULONGLONG)now.QuadPart * 1000 / (ULONGLONG)freq.QuadPart;
}

uint64_t hs_millis(void)
{
    return GetTickCount64_();
}

int hs_poll(const hs_descriptor_set *set, int timeout)
{
    assert(set);
    assert(set->count);
    assert(set->count <= 64);

    DWORD ret = WaitForMultipleObjects((DWORD)set->count, set->desc, FALSE,
                                       timeout < 0 ? INFINITE : (DWORD)timeout);
    switch (ret) {
    case WAIT_FAILED:
        return hs_error(HS_ERROR_SYSTEM, "WaitForMultipleObjects() failed: %s",
                        hs_win32_strerror(0));
    case WAIT_TIMEOUT:
        return 0;
    }

    return set->id[ret - WAIT_OBJECT_0];
}

char *hs_win32_strerror(DWORD err)
{
    static char buf[2048];
    char *ptr;
    DWORD r;

    if (!err)
        err = GetLastError();

    r = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                      err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), NULL);

    if (r) {
        ptr = buf + strlen(buf);
        // FormatMessage adds newlines, remove them
        while (ptr > buf && (ptr[-1] == '\n' || ptr[-1] == '\r'))
            ptr--;
        *ptr = 0;
    } else {
        strcpy(buf, "(unknown)");
    }

    return buf;
}

bool hs_win32_test_version(hs_win32_version version)
{
    OSVERSIONINFOEX info = {0};
    DWORDLONG cond = 0;

    switch (version) {
    case HS_WIN32_VERSION_XP:
        info.dwMajorVersion = 5;
        info.dwMinorVersion = 1;
        break;
    case HS_WIN32_VERSION_VISTA:
        info.dwMajorVersion = 6;
        break;
    case HS_WIN32_VERSION_7:
        info.dwMajorVersion = 6;
        info.dwMinorVersion = 1;
        break;
    case HS_WIN32_VERSION_8:
        info.dwMajorVersion = 6;
        info.dwMinorVersion = 2;
        break;
    case HS_WIN32_VERSION_10:
        info.dwMajorVersion = 10;
        info.dwMinorVersion = 0;
        break;
    }

    VER_SET_CONDITION(cond, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(cond, VER_MINORVERSION, VER_GREATER_EQUAL);

    return VerifyVersionInfo(&info, VER_MAJORVERSION | VER_MINORVERSION, cond);
}
