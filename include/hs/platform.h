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

#ifndef HS_PLATFORM_H
#define HS_PLATFORM_H

#include "common.h"

HS_BEGIN_C

#ifdef _WIN32
/**
 * @ingroup misc
 * @brief Common Windows version numbers.
 */
enum hs_win32_release {
    /** Windows 2000. */
    HS_WIN32_VERSION_2000 = 500,
    /** Windows XP. */
    HS_WIN32_VERSION_XP = 501,
    /** Windows Server 2003 or XP-64. */
    HS_WIN32_VERSION_2003 = 502,
    /** Windows Vista. */
    HS_WIN32_VERSION_VISTA = 600,
    /** Windows 7 */
    HS_WIN32_VERSION_7 = 601,
    /** Windows 8 */
    HS_WIN32_VERSION_8 = 602,
    /** Windows 8.1 */
    HS_WIN32_VERSION_8_1 = 603,
    /** Windows 10 */
    HS_WIN32_VERSION_10 = 1000
};
#endif

/**
 * @ingroup misc
 * @brief Descriptor set for cross-platform device polling.
 *
 * The descriptor set and polling helpers are provided for convenience, but they are neither
 * fast nor powerful. Use more advanced event libraries (libev, libevent, libuv) or the OS-specific
 * functions if you need more.
 *
 * You can manipulate this structure directly but helper functions are also provided. It is
 * required to set hs->count to 0 initially.
 *
 * @sa hs_descriptor
 * @sa hs_descriptor_set_clear()
 * @sa hs_descriptor_set_add()
 * @sa hs_descriptor_set_remove()
 * @sa hs_poll()
 */
typedef struct hs_descriptor_set {
    /** Number of descriptors. */
    unsigned int count;

    /** Descriptor (int on POSIX platforms, HANDLE on Windows). */
    hs_descriptor desc[64];
    /** Value returned by hs_poll() when a descriptor becomes readable. */
    int id[64];
} hs_descriptor_set;

/**
 * @{
 * @name System Functions
 */

/**
 * @ingroup misc
 * @brief Get time from a monotonic clock.
 *
 * You should not rely on the absolute value, whose meaning may differ on various platforms.
 * Use it to calculate periods and durations.
 *
 * While the returned value is in milliseconds, the resolution is not that good on some
 * platforms. On Windows, it is over 10 milliseconds.
 *
 * @return This function returns a mononotic time value in milliseconds.
 */
HS_PUBLIC uint64_t hs_millis(void);

/**
 * @ingroup misc
 * @brief Adjust a timeout over a time period.
 *
 * This function returns -1 if the timeout is negative. Otherwise, it decreases the timeout
 * for each millisecond elapsed since @p start. When @p timeout milliseconds have passed,
 * the function returns 0.
 *
 * hs_millis() is used as the time source, so you must use it for @p start.
 *
 * @code{.c}
 * uint64_t start = hs_millis();
 * do {
 *     r = poll(&pfd, 1, hs_adjust_timeout(timeout, start));
 * } while (r < 0 && errno == EINTR);
 * @endcode
 *
 * This function is mainly used in libhs to restart interrupted system calls with
 * timeouts, such as poll().
 *
 * @param timeout Timeout is milliseconds.
 * @param start Start of the timeout period, from hs_millis().
 *
 * @return This function returns the adjusted value, or -1 if @p timeout is negative.
 */
HS_PUBLIC int hs_adjust_timeout(int timeout, uint64_t start);

#ifdef __linux__
/**
 * @ingroup misc
 * @brief Get the Linux kernel version as a composite decimal number.
 *
 * For pre-3.0 kernels, the value is MMmmrrppp (2.6.34.1 => 020634001). For recent kernels,
 * it is MMmm00ppp (4.1.2 => 040100002).
 *
 * Do not rely on this too much, because kernel versions do not reflect the different kernel
 * flavors. Some distributions use heavily-patched builds, with a lot of backported code. When
 * possible, detect functionalities instead.
 *
 * @return This function returns the version number.
 */
HS_PUBLIC uint32_t hs_linux_version(void);
#endif

#ifdef _WIN32
/**
 * @ingroup misc
 * @brief Format an error string using FormatMessage().
 *
 * The content is only valid until the next call to hs_win32_strerror(), be careful with
 * multi-threaded code.
 *
 * @param err Windows error code, or use 0 to get it from GetLastError().
 * @return This function returns a private buffer containing the error string, valid until the
 *     next call to hs_win32_strerror().
 */
HS_PUBLIC const char *hs_win32_strerror(unsigned long err);
/**
 * @ingroup misc
 * @brief Get the Windows version as a composite decimal number.
 *
 * The value is MMmm, see https://msdn.microsoft.com/en-us/library/windows/desktop/ms724832%28v=vs.85%29.aspx
 * for the operating system numbers. You can use the predefined enum values from
 * @ref hs_win32_release.
 *
 * Use this only when testing for functionality is not possible or impractical.
 *
 * @return This function returns the version number.
 */
HS_PUBLIC uint32_t hs_win32_version(void);
#endif

#ifdef __APPLE__
/**
 * @ingroup misc
 * @brief Get the Darwin version on Apple systems
 *
 * The value is MMmmrr (11.4.2 => 110402), see https://en.wikipedia.org/wiki/Darwin_%28operating_system%29
 * for the corresponding OS X releases.
 *
 * @return This function returns the version number.
 */
HS_PUBLIC uint32_t hs_darwin_version(void);
#endif

/** @} */

/**
 * @{
 * @name Polling Functions
 */

/**
 * @ingroup misc
 * @brief Remove all descriptors from this set.
 *
 * In effect, this only sets set->count to 0. You can do it directly if you prefer.
 *
 * @param set Descriptor set.
 *
 * @sa hs_descriptor_set
 */
HS_PUBLIC void hs_descriptor_set_clear(hs_descriptor_set *set);
/**
 * @ingroup misc
 * @brief Add a descriptor to a set.
 *
 * Overflow will throw an assert in debug builds, the behavior is undefined in release builds.
 *
 * @param set  Descriptor set.
 * @param desc Descriptor (int on POSIX platforms, HANDLE on Windows).
 * @param id   Value returned by hs_poll() when this descriptor becomes readable. This values does
 *     not need to be unique.
 *
 * @sa hs_descriptor_set
 */
HS_PUBLIC void hs_descriptor_set_add(hs_descriptor_set *set, hs_descriptor desc, int id);
/**
 * @ingroup misc
 * @brief Remove descriptors from a set.
 *
 * All descriptors sharing the value @param id will be removed.
 *
 * @param set Descriptor set.
 * @param id  ID value associated with the descriptors to remove.
 *
 * @sa hs_descriptor_set
 */
HS_PUBLIC void hs_descriptor_set_remove(hs_descriptor_set *set, int id);

/**
 * @ingroup misc
 * @brief Wait on a descriptor set for readable descriptors.
 *
 * @param set     Descriptor set.
 * @param timeout Timeout in milliseconds, or -1 to block indefinitely.
 * @return This function returns the value associated to the first descriptor to become readable.
 *     It returns 0 on timeout, or a negative @ref hs_error_code value.
 *
 * @sa hs_descriptor_set
 */
HS_PUBLIC int hs_poll(const hs_descriptor_set *set, int timeout);

HS_END_C

#endif
