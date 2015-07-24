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

#ifndef HS_COMMON_H
#define HS_COMMON_H

#include <stddef.h>
#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
    #define HS_BEGIN_C extern "C" {
    #define HS_END_C }
#else
    #define HS_BEGIN_C
    #define HS_END_C
#endif

HS_BEGIN_C

/**
 * @defgroup misc Miscellaneous
 */

 /**
  * @ingroup misc
  * @brief Compile-time libhs version.
  *
  * The version is represented as a six-digit decimal value respecting **semantic versioning**:
  * MMmmpp (major, minor, patch), e.g. 900 for "0.9.0", 10002 for "1.0.2" or 220023 for "22.0.23".
  *
  * @sa hs_version() for the run-time version.
  * @sa HS_VERSION_STRING for the version string.
  */
#define HS_VERSION 900
  /**
   * @ingroup misc
   * @brief Compile-time libhs version string.
   *
   * @sa hs_version_string() for the run-time version.
   * @sa HS_VERSION for the version number.
   */
#define HS_VERSION_STRING "0.9.0"

#if defined(__GNUC__)
    #define HS_PUBLIC __attribute__((__visibility__("default")))

    #ifdef __MINGW_PRINTF_FORMAT
        #define HS_PRINTF_FORMAT(fmt, first) __attribute__((__format__(__MINGW_PRINTF_FORMAT, fmt, first)))
    #else
        #define HS_PRINTF_FORMAT(fmt, first) __attribute__((__format__(__printf__, fmt, first)))
    #endif
#elif _MSC_VER >= 1900
    #ifdef _HS_UTIL_H
        #define HS_PUBLIC __declspec(dllexport)
    #else
        #define HS_PUBLIC __declspec(dllimport)
    #endif

    #define HS_PRINTF_FORMAT(fmt, first)

    // HAVE_SSIZE_T is used this way by other projects
    #ifndef HAVE_SSIZE_T
        #define HAVE_SSIZE_T
        #ifdef _WIN64
typedef __int64 ssize_t;
        #else
typedef long ssize_t;
        #endif
    #endif
#else
    #error "This compiler is not supported"
#endif

#if defined(DOXYGEN)
/**
 * @ingroup misc
 * @brief Type representing an OS descriptor/handle.
 *
 * This is used in functions taking or returning an OS descriptor/handle, in order to avoid
 * having different function prototypes on different platforms.
 *
 * The underlying type is:
 * - int on POSIX platforms, including OS X
 * - HANDLE (aka. void *) on Windows
 */
typedef _platform_specific_ hs_descriptor;
#elif defined(_WIN32)
typedef void *hs_descriptor; // HANDLE
#else
typedef int hs_descriptor;
#endif

/**
 * @ingroup misc
 * @brief libhs error codes.
 */
typedef enum hs_err {
    /** Memory error. */
    HS_ERROR_MEMORY        = -1,
    /** Missing resource error. */
    HS_ERROR_NOT_FOUND     = -2,
    /** Permission denied. */
    HS_ERROR_ACCESS        = -3,
    /** Input/output error. */
    HS_ERROR_IO            = -4,
    /** Generic system error. */
    HS_ERROR_SYSTEM        = -5,
    /** Invalid data error. */
    HS_ERROR_INVALID       = -6
} hs_err;

typedef void hs_error_func(hs_err err, const char *msg, void *udata);

/**
 * @{
 * @name Version Functions
 */

/**
 * @ingroup misc
 * @brief Run-time libhs version.
 *
  * The version is represented as a six-digit decimal value respecting **semantic versioning**:
  * MMmmpp (major, minor, patch), e.g. 900 for "0.9.0", 10002 for "1.0.2" or 220023 for "22.0.23".
  *
  * @return This function returns the run-time version number.
  *
  * @sa HS_VERSION for the compile-time version.
  * @sa hs_version_string() for the version string.
 */
HS_PUBLIC uint32_t hs_version(void);
/**
 * @ingroup misc
 * @brief Run-time libhs version string.
 *
 * @return This function returns the run-time version string.
 *
 * @sa HS_VERSION_STRING for the compile-time version.
 * @sa hs_version() for the version number.
 */
HS_PUBLIC const char *hs_version_string(void);

/** @} */

/**
 * @{
 * @name Error Functions
 */

/**
 * @ingroup misc
 * @brief Change the error message handler.
 *
 * The default handler prints the message to stderr.
 *
 * @param f     New error message handler, or NULL to restore the default handler.
 * @param udata Pointer to user-defined arbitrary data for the message handler.
 *
 * @sa hs_error()
 */
HS_PUBLIC void hs_error_redirect(hs_error_func *f, void *udata);
/**
 * @ingroup misc
 * @brief Mask an error code.
 *
 * Mask error codes to prevent libhs from calling the error message handler (the default one
 * simply prints the string to stderr). It does not change the behavior of the function where
 * the error occurs.
 *
 * For example, if you want to open a device without a missing device message, you can use:
 * @code{.c}
 * hs_error_mask(HS_ERROR_NOT_FOUND);
 * r = hs_device_open(dev, &h);
 * hs_error_unmask();
 * if (r < 0)
 *     return r;
 * @endcode
 *
 * The masked codes are kept in a limited stack, you must not forget to unmask codes quickly
 * with @ref hs_error_unmask().
 *
 * @param err Error code to mask.
 *
 * @sa hs_error_unmask()
 */
HS_PUBLIC void hs_error_mask(hs_err err);
/**
 * @ingroup misc
 * @brief Unmask the last masked error code.
 *
 * @sa hs_error_mask()
 */
HS_PUBLIC void hs_error_unmask(void);

/**
 * @ingroup misc
 * @brief Call the error message handler with a printf-formatted message.
 *
 * Format an error message and call the error handler with it. Pass NULL to @p fmt to use a
 * generic error message. The default handler prints it to stderr, see hs_error_redirect().
 *
 * The error code is simply returned as a convenience, so you can use this function like:
 * @code{.c}
 * // Explicit error message
 * int pipe[2], r;
 * r = pipe(pipe);
 * if (r < 0)
 *     return hs_error(HS_ERROR_SYSTEM, "pipe() failed: %s", strerror(errno));
 *
 * // Generic error message (e.g. "Memory error")
 * char *p = malloc(128);
 * if (!p)
 *     return hs_error(HS_ERROR_MEMORY, NULL);
 * @endcode
 *
 * @param err Error code.
 * @param fmt Format string, using printf syntax.
 * @param ...
 * @return This function returns the error code.
 *
 * @sa hs_error_mask() to mask specific error codes.
 * @sa hs_error_redirect() to use a custom message handler.
 */
HS_PUBLIC int hs_error(hs_err err, const char *fmt, ...) HS_PRINTF_FORMAT(2, 3);

HS_END_C

#endif
