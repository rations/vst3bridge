/*
 * wine_win32_prelude.h — force-included first in every Wine host translation unit.
 *
 * winsock2.h must be processed before any C++ stdlib header that drags in
 * sys/select.h (glibc 2.38+: stdlib.h → sys/types.h → sys/select.h defines
 * FD_CLR as an error sentinel that collides with winsock2's fd_set).
 * Using -include ensures this header is always first, regardless of per-file
 * include order.
 */
#pragma once

#ifdef BUILDING_WINE_HOST
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// The VST3 SDK's pluginterfaces/base/ftypes.h defines UNICODE 1 late in the
// include chain, after windows.h has already resolved WINELIB_NAME_AW macros
// (e.g. FoldString → FoldStringA vs FoldStringW).  Define UNICODE here so
// all Windows API name-mangling macros resolve to the W (Unicode) variants.
#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE
#define _UNICODE 1
#endif
// Pull in winsock2.h before any C++ stdlib header that drags in sys/select.h.
// Only winsock2.h is needed here — windows.h is included by individual files
// that actually need Win32 APIs, avoiding its macro pollution in SDK headers.
#include <winsock2.h>

// wineg++ maps __declspec(x) → __declspec_##x and defines __declspec_deprecated
// as an *object-like* macro __attribute__((deprecated)).  When the VST3 SDK
// writes  __declspec(deprecated("msg"))  wineg++ expands it to
//   __declspec_deprecated("msg")
// which calls __attribute__((deprecated)) as a function → parse error.
// Redefine as a variadic function-like macro so the message form is valid.
#ifdef __WINE__
#undef  __declspec_deprecated
#define __declspec_deprecated(...) __attribute__((deprecated(__VA_ARGS__)))

// Windows/MSVCRT string functions used by the Steinberg VST3 SDK under
// SMTG_OS_WINDOWS.  Wine's compiler sets _GNU_SOURCE so wcscasecmp and
// wcsncasecmp are available from <wchar.h>.
#include <strings.h>  // strcasecmp, strncasecmp
#include <wchar.h>    // wcscasecmp, wcsncasecmp, vswprintf
#ifndef stricmp
#define stricmp  strcasecmp
#endif
#ifndef strnicmp
#define strnicmp strncasecmp
#endif
#ifndef wcsicmp
#define wcsicmp  wcscasecmp
#endif
#ifndef wcsnicmp
#define wcsnicmp wcsncasecmp
#endif
// _vsnwprintf(buf, n, fmt, va) — wrap POSIX vswprintf (same semantics under Wine)
#ifndef _vsnwprintf
#define _vsnwprintf(buf, n, fmt, ap) vswprintf((buf), (n), (fmt), (ap))
#endif
#endif  // __WINE__

#endif
