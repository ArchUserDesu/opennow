#pragma once
/* Compatibility shims for POSIX-oriented C dependencies when built by the Xbox 360 XDK. */
#ifdef _XBOX
#include <xtl.h>
#include <winsockx.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef ssize_t
typedef int ssize_t;
#endif
#ifndef socklen_t
typedef int socklen_t;
#endif
#ifndef inline
#define inline __inline
#endif
#ifndef va_copy
#define va_copy(dst, src) ((dst) = (src))
#endif
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#ifndef close
#define close closesocket
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif
#ifndef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#endif
#ifndef EAGAIN
#define EAGAIN WSAEWOULDBLOCK
#endif
#ifndef snprintf
#define snprintf _snprintf
#endif

#ifdef __cplusplus
extern "C" {
#endif
int usleep(unsigned int usec);
int gettimeofday(struct timeval* tv, void* tz);
#ifdef __cplusplus
}
#endif
#endif
