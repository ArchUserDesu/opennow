#ifndef OPENNOW_XDK_SRTP_CONFIG_H
#define OPENNOW_XDK_SRTP_CONFIG_H
/* Xbox 360 / MSVC configuration for libsrtp's portable C crypto core. */
#define CPU_RISC 1
#define HAVE_INT8_T 1
#define HAVE_INT16_T 1
#define HAVE_INT32_T 1
#define HAVE_UINT8_T 1
#define HAVE_UINT16_T 1
#define HAVE_UINT32_T 1
#define HAVE_UINT64_T 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define HAVE_SYS_TYPES_H 1
#define STDC_HEADERS 1
#define SIZEOF_UNSIGNED_LONG 4
#define SIZEOF_UNSIGNED_LONG_LONG 8
#define WORDS_BIGENDIAN 1
/* Deliberately leave OPENSSL/NSS/GCM undefined: use libsrtp's built-in AES/SHA1. */
#endif
