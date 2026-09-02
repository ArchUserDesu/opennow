#pragma once
/* Start from the upstream default config, then tailor it for an Xbox title. */
#include <mbedtls/mbedtls_config.h>

/* The Xbox 360 CPU is PowerPC; disable x86 and ARM crypto backends. */
#ifdef MBEDTLS_AESNI_C
#undef MBEDTLS_AESNI_C
#endif
#ifdef MBEDTLS_AESCE_C
#undef MBEDTLS_AESCE_C
#endif
#ifdef MBEDTLS_PADLOCK_C
#undef MBEDTLS_PADLOCK_C
#endif
#ifdef MBEDTLS_PSA_CRYPTO_C
#undef MBEDTLS_PSA_CRYPTO_C
#endif
#ifdef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#endif
#ifdef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_ITS_FILE_C
#endif
#ifdef MBEDTLS_LMS_C
#undef MBEDTLS_LMS_C
#endif
#ifdef MBEDTLS_SSL_PROTO_TLS1_3
#undef MBEDTLS_SSL_PROTO_TLS1_3
#endif
#ifdef MBEDTLS_ARIA_C
#undef MBEDTLS_ARIA_C
#endif
#ifdef MBEDTLS_CAMELLIA_C
#undef MBEDTLS_CAMELLIA_C
#endif
#ifdef MBEDTLS_CCM_C
#undef MBEDTLS_CCM_C
#endif
#ifdef MBEDTLS_CHACHA20_C
#undef MBEDTLS_CHACHA20_C
#endif
#ifdef MBEDTLS_CHACHAPOLY_C
#undef MBEDTLS_CHACHAPOLY_C
#endif
#ifdef MBEDTLS_DES_C
#undef MBEDTLS_DES_C
#endif
#ifdef MBEDTLS_NIST_KW_C
#undef MBEDTLS_NIST_KW_C
#endif
#ifdef MBEDTLS_CIPHER_MODE_XTS
#undef MBEDTLS_CIPHER_MODE_XTS
#endif
#ifdef MBEDTLS_CMAC_C
#undef MBEDTLS_CMAC_C
#endif
#ifdef MBEDTLS_SELF_TEST
#undef MBEDTLS_SELF_TEST
#endif
#ifdef MBEDTLS_DEBUG_C
#undef MBEDTLS_DEBUG_C
#endif
#ifdef MBEDTLS_DHM_C
#undef MBEDTLS_DHM_C
#endif
#ifdef MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#endif
#ifdef MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED
#endif
#ifdef MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
#endif
#ifdef MBEDTLS_KEY_EXCHANGE_RSA_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_RSA_PSK_ENABLED
#endif
#ifdef MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#endif
#ifdef MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED
#endif
#ifdef MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED
#endif
#ifdef MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED
#endif
#ifdef MBEDTLS_MD5_C
#undef MBEDTLS_MD5_C
#endif
#ifdef MBEDTLS_RIPEMD160_C
#undef MBEDTLS_RIPEMD160_C
#endif
#ifdef MBEDTLS_SHA1_C
#undef MBEDTLS_SHA1_C
#endif
#ifdef MBEDTLS_SHA384_C
#undef MBEDTLS_SHA384_C
#endif
#ifdef MBEDTLS_SHA512_C
#undef MBEDTLS_SHA512_C
#endif
#ifdef MBEDTLS_PKCS7_C
#undef MBEDTLS_PKCS7_C
#endif
#ifdef MBEDTLS_FS_IO
#undef MBEDTLS_FS_IO
#endif
#ifdef MBEDTLS_PKCS5_C
#undef MBEDTLS_PKCS5_C
#endif
#ifdef MBEDTLS_PKCS12_C
#undef MBEDTLS_PKCS12_C
#endif

/* libpeer needs DTLS-SRTP. */
#ifndef MBEDTLS_SSL_DTLS_SRTP
#define MBEDTLS_SSL_DTLS_SRTP
#endif

/* OpenNOW supplies mbedtls_hardware_poll() from XeCryptRandom. */
#ifndef MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#endif

/* We use Xbox Winsock through custom BIO callbacks, not mbedTLS net_sockets.c. */
#ifdef MBEDTLS_NET_C
#undef MBEDTLS_NET_C
#endif
