#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "address.h"
#include "utils.h"

void addr_set_family(Address* addr, int family) {
#if defined(_XBOX)
  (void)family;
  addr->family = AF_INET;
#else
  switch (family) {
    case AF_INET6:
      addr->family = AF_INET6;
      break;
    case AF_INET:
    default:
      addr->family = AF_INET;
      break;
  }
#endif
}

void addr_set_port(Address* addr, uint16_t port) {
  addr->port = port;
#if defined(_XBOX)
  addr->sin.sin_port = htons(port);
#else
  switch (addr->family) {
    case AF_INET6:
      addr->sin6.sin6_port = htons(port);
      break;
    case AF_INET:
    default:
      addr->sin.sin_port = htons(port);
      break;
  }
#endif
}

int addr_from_string(const char* buf, Address* addr) {
#if defined(_XBOX)
  unsigned long value = inet_addr(buf);
  if (value == INADDR_NONE && strcmp(buf, "255.255.255.255") != 0) {
    return 0;
  }
  memset(addr, 0, sizeof(*addr));
  addr->family = AF_INET;
  addr->sin.sin_family = AF_INET;
  addr->sin.sin_addr.s_addr = value;
  return 1;
#else
  if (inet_pton(AF_INET, buf, &(addr->sin.sin_addr)) == 1) {
    addr_set_family(addr, AF_INET);
    return 1;
  } else if (inet_pton(AF_INET6, buf, &(addr->sin6.sin6_addr)) == 1) {
    addr_set_family(addr, AF_INET6);
    return 1;
  }
  return 0;
#endif
}

int addr_to_string(const Address* addr, char* buf, size_t len) {
#if defined(_XBOX)
  const char* text = inet_ntoa(addr->sin.sin_addr);
  if (!text || len == 0) return 0;
  strncpy(buf, text, len - 1);
  buf[len - 1] = '\0';
  return 1;
#else
  memset(buf, 0, len);
  switch (addr->family) {
    case AF_INET6:
      return inet_ntop(AF_INET6, &addr->sin6.sin6_addr, buf, len) != NULL;
    case AF_INET:
    default:
      return inet_ntop(AF_INET, &addr->sin.sin_addr, buf, len) != NULL;
  }
#endif
}

int addr_equal(const Address* a, const Address* b) {
#if defined(_XBOX)
  return a->family == b->family && a->port == b->port &&
         a->sin.sin_addr.s_addr == b->sin.sin_addr.s_addr;
#else
  // TODO
  return 1;
#endif
}
