#pragma once
/* Compatibility shims for POSIX-oriented C dependencies when built by the Xbox 360 XDK. */
#ifdef _XBOX
#include <xtl.h>
#include <winsockx.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

/*
 * libpeer is C99-style C, but the Xbox 360 XDK C frontend is an old MSVC C89
 * compiler.  We compile only libpeer through the XDK C++ frontend so mixed
 * declarations and for-loop declarations are accepted.  C permits implicit
 * conversion from void* returned by malloc/calloc/realloc while C++ does not;
 * this tiny conversion wrapper preserves the C allocation call sites without
 * touching every translation unit.  It is enabled only for libpeer.
 */
#if defined(__cplusplus) && defined(OPENNOW_PEER_CPP)
class opennow_xdk_alloc_result {
 public:
  explicit opennow_xdk_alloc_result(void* p) : p_(p) {}
  template <typename T> operator T*() const { return static_cast<T*>(p_); }
  operator void*() const { return p_; }
 private:
  void* p_;
};

static __inline opennow_xdk_alloc_result opennow_xdk_malloc(size_t size) {
  return opennow_xdk_alloc_result(::malloc(size));
}
static __inline opennow_xdk_alloc_result opennow_xdk_calloc(size_t count, size_t size) {
  return opennow_xdk_alloc_result(::calloc(count, size));
}
static __inline opennow_xdk_alloc_result opennow_xdk_realloc(void* ptr, size_t size) {
  return opennow_xdk_alloc_result(::realloc(ptr, size));
}
#define malloc(size) opennow_xdk_malloc(size)
#define calloc(count, size) opennow_xdk_calloc((count), (size))
#define realloc(ptr, size) opennow_xdk_realloc((ptr), (size))
#endif
#endif
