#ifndef ADDRESS_H_
#define ADDRESS_H_

#include "config.h"
#if defined(_XBOX)
#include <winsockx.h>
#elif CONFIG_USE_LWIP
#include <lwip/sockets.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <stdint.h>
#include <stddef.h>

#if defined(_XBOX)
#define ADDRSTRLEN 16
#else
#define ADDRSTRLEN INET6_ADDRSTRLEN
#endif

typedef struct Address {
  uint8_t family;
  struct sockaddr_in sin;
#if !defined(_XBOX)
  struct sockaddr_in6 sin6;
#endif
  uint16_t port;
} Address;

void addr_set_family(Address* addr, int family);

void addr_set_port(Address* addr, uint16_t port);

int addr_inet6_validate(const char* ipv6, size_t len, Address* addr);

int addr_inet_validate(const char* ipv4, size_t len, Address* addr);

int addr_to_string(const Address* addr, char* buf, size_t len);

int addr_from_string(const char* str, Address* addr);

int addr_equal(const Address* a, const Address* b);

#endif  // ADDRESS_H_
