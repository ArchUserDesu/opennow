#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"

#if CONFIG_USE_LWIP
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#elif defined(__SWITCH__) || defined(_XBOX)
#include <unistd.h>
#include <netdb.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/ioctl.h>
#endif

#include "ports.h"
#include "utils.h"

int ports_get_host_addr(Address* addr, const char* iface_prefix) {
  int ret = 0;

#if CONFIG_USE_LWIP
  struct netif* netif;
  int i;
  for (netif = netif_list; netif != NULL; netif = netif->next) {
    switch (addr->family) {
      case AF_INET6:
        for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
          if (!ip6_addr_isany(netif_ip6_addr(netif, i))) {
            memcpy(&addr->sin6.sin6_addr, netif_ip6_addr(netif, i), 16);
            ret = 1;
            break;
          }
        }
        break;
      case AF_INET:
      default:
        if (!ip_addr_isany(&netif->ip_addr)) {
          memcpy(&addr->sin.sin_addr, &netif->ip_addr.u_addr.ip4, 4);
          ret = 1;
        }
        break;
    }

    if (ret) {
      break;
    }
  }
#elif defined(_XBOX)
  XNADDR xnaddr;
  DWORD status = XNetGetTitleXnAddr(&xnaddr);
  if (status != XNET_GET_XNADDR_NONE && status != XNET_GET_XNADDR_PENDING &&
      addr->family == AF_INET) {
    addr->sin.sin_addr = xnaddr.ina;
    ret = 1;
  }
#elif defined(__SWITCH__)
  uint32_t ip = gethostid();
  if (ip != 0 && addr->family == AF_INET) {
      memcpy(&addr->sin.sin_addr, &ip, 4);
      ret = 1;
  }
#else

  struct ifaddrs *ifaddr, *ifa;

  if (getifaddrs(&ifaddr) == -1) {
    LOGE("getifaddrs failed: %s", strerror(errno));
    return -1;
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) {
      continue;
    }

    if (ifa->ifa_addr->sa_family != addr->family) {
      continue;
    }

    if (iface_prefix && strlen(iface_prefix) > 0) {
      if (strncmp(ifa->ifa_name, iface_prefix, strlen(iface_prefix)) != 0) {
        continue;
      }

    } else {
      if ((ifa->ifa_flags & IFF_UP) == 0) {
        continue;
      }

      if ((ifa->ifa_flags & IFF_RUNNING) == 0) {
        continue;
      }

      if ((ifa->ifa_flags & IFF_LOOPBACK) == IFF_LOOPBACK) {
        continue;
      }
    }

    switch (ifa->ifa_addr->sa_family) {
      case AF_INET6:
        memcpy(&addr->sin6, ifa->ifa_addr, sizeof(struct sockaddr_in6));
        break;
      case AF_INET:
      default:
        memcpy(&addr->sin, ifa->ifa_addr, sizeof(struct sockaddr_in));
        break;
    }
    ret = 1;
    break;
  }
  freeifaddrs(ifaddr);
#endif
  return ret;
}

int ports_resolve_addr(const char* host, Address* addr) {
  char addr_string[ADDRSTRLEN];
  int ret = -1;
#ifdef _XBOX
  IN_ADDR resolved;
  const char* lookup_host = host;
  memset(addr, 0, sizeof(*addr));
  resolved.s_addr = inet_addr(host);
  if (resolved.s_addr == INADDR_NONE) {
    XNDNS* dns = NULL;
    /* Xbox DNS returns WSAHOST_NOT_FOUND for NVIDIA's s1 alias even though
       its canonical name resolves.  Resolve the canonical target directly. */
    if (strcmp(host, "s1.stun.gamestream.nvidia.com") == 0) {
      lookup_host = "nv.stun.gamestream.nvidia.com";
      LOGI("STUN DNS alias normalized %s -> %s", host, lookup_host);
    }
    int status = XNetDnsLookup(lookup_host, NULL, &dns);
    if (status != 0 || dns == NULL) {
      LOGE("XNetDnsLookup submit failed host=%s lookup=%s status=%d wsa=%d", host, lookup_host, status,
           WSAGetLastError());
      return ret;
    }
    while (dns->iStatus == WSAEINPROGRESS)
      Sleep(1);
    if (dns->iStatus == 0 && dns->cina > 0)
      resolved = dns->aina[0];
    else {
      LOGE("XNetDnsLookup failed host=%s lookup=%s status=%d addresses=%u", host,
           lookup_host, dns->iStatus, (unsigned)dns->cina);
      resolved.s_addr = INADDR_NONE;
    }
    XNetDnsRelease(dns);
  }
  if (resolved.s_addr != INADDR_NONE) {
    addr_set_family(addr, AF_INET);
    addr->sin.sin_family = AF_INET;
    addr->sin.sin_addr = resolved;
    ret = 0;
  }
#else
  struct addrinfo hints, *res, *p;
  int status;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((status = getaddrinfo(host, NULL, &hints, &res)) != 0) {
    LOGE("getaddrinfo error: %d\n", status);
    return ret;
  }

  // TODO: Support for IPv6
  addr_set_family(addr, AF_INET);
  for (p = res; p != NULL; p = p->ai_next) {
    if (p->ai_family == addr->family) {
      switch (addr->family) {
        case AF_INET6:
          memcpy(&addr->sin6, p->ai_addr, sizeof(struct sockaddr_in6));
          break;
        case AF_INET:
        default:
          memcpy(&addr->sin, p->ai_addr, sizeof(struct sockaddr_in));
          break;
      }
      ret = 0;
    }
  }

  addr_to_string(addr, addr_string, sizeof(addr_string));
  LOGI("Resolved %s -> %s", host, addr_string);
  freeaddrinfo(res);
#endif
  if (ret == 0) {
    addr_to_string(addr, addr_string, sizeof(addr_string));
    LOGI("Resolved %s -> %s raw=0x%08lx", host, addr_string,
         addr->sin.sin_addr.s_addr);
  }
  return ret;
}

uint32_t ports_get_epoch_time() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void ports_sleep_ms(int ms) {
#if CONFIG_USE_LWIP
  sys_msleep(ms);
#else
  usleep(ms * 1000);
#endif
}
