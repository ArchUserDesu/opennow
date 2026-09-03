#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "socket.h"
#include "utils.h"

int udp_socket_add_multicast_group(UdpSocket* udp_socket, Address* mcast_addr) {
#ifdef _XBOX
  (void)udp_socket;
  (void)mcast_addr;
  return -1;
#else
  int ret = 0;
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};

  imreq.imr_interface.s_addr = INADDR_ANY;
  // IPV4 only
  imreq.imr_multiaddr.s_addr = mcast_addr->sin.sin_addr.s_addr;

  if ((ret = setsockopt(udp_socket->fd, IPPROTO_IP, IP_MULTICAST_IF, &iaddr, sizeof(struct in_addr))) < 0) {
    LOGE("Failed to set IP_MULTICAST_IF: %d", ret);
    return ret;
  }

  if ((ret = setsockopt(udp_socket->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq))) < 0) {
    LOGE("Failed to set IP_ADD_MEMBERSHIP: %d", ret);
    return ret;
  }

  return 0;
#endif
}

int udp_socket_open(UdpSocket* udp_socket, int family, int port) {
  int ret;
  int reuse = 1;
  int receive_buffer = 2 * 1024 * 1024;
  struct sockaddr* sa;
  socklen_t sock_len;

  memset(&udp_socket->bind_addr, 0, sizeof(udp_socket->bind_addr));
#ifdef _XBOX
  memset(&udp_socket->connected_addr, 0, sizeof(udp_socket->connected_addr));
  udp_socket->connected = 0;
#endif
  udp_socket->bind_addr.family = family;
  switch (family) {
#if CONFIG_IPV6
    case AF_INET6:
      udp_socket->fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
      udp_socket->bind_addr.sin6.sin6_family = AF_INET6;
      udp_socket->bind_addr.sin6.sin6_port = htons(port);
      udp_socket->bind_addr.sin6.sin6_addr = in6addr_any;
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin6.sin6_port);
      sa = (struct sockaddr*)&udp_socket->bind_addr.sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
#endif
    case AF_INET:
    default:
      /* XNet does not reliably infer UDP when protocol is zero on retail
         kernels.  An inferred secure datagram socket rejects raw Internet
         destinations with WSAEHOSTUNREACH even when the title has the
         insecure-sockets privilege. */
      udp_socket->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      udp_socket->bind_addr.sin.sin_family = AF_INET;
      udp_socket->bind_addr.sin.sin_port = htons(port);
      udp_socket->bind_addr.sin.sin_addr.s_addr = htonl(INADDR_ANY);
      sa = (struct sockaddr*)&udp_socket->bind_addr.sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  if (udp_socket->fd < 0) {
    LOGE("Failed to create socket");
    return -1;
  }

#ifdef _XBOX
  {
    BOOL enabled = TRUE;
    int opt5801 = setsockopt(udp_socket->fd, SOL_SOCKET, 0x5801, (const char*)&enabled, sizeof(enabled));
    int err5801 = opt5801 == 0 ? 0 : WSAGetLastError();
    LOGI("XNet UDP direct-outbound option fd=%d opt5801=%d/%d",
         udp_socket->fd, opt5801, err5801);
    /* 0x5801 is the raw Internet bypass.  Do not apply 0x5802 here: on the
       retail UDP stack it selects secure-peer routing, causing raw IPv4
       destinations to enter XNET_CONNECT_STATUS_LOST. */
  }
#endif

  do {
    if (setsockopt(udp_socket->fd, SOL_SOCKET, SO_RCVBUF,
                   (const char*)&receive_buffer, sizeof(receive_buffer)) < 0) {
      LOGW("Failed to enlarge UDP receive buffer: %s", strerror(errno));
    } else {
      socklen_t option_size = sizeof(receive_buffer);
      if (getsockopt(udp_socket->fd, SOL_SOCKET, SO_RCVBUF,
                     (char*)&receive_buffer, &option_size) == 0) {
        LOGI("UDP receive buffer: %d bytes", receive_buffer);
      }
    }

    if ((ret = setsockopt(udp_socket->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse))) < 0) {
      LOGW("reuse failed. ignore");
    }

    if ((ret = bind(udp_socket->fd, sa, sock_len)) < 0) {
      LOGE("Failed to bind socket: %d", ret);
      break;
    }

    if (getsockname(udp_socket->fd, sa, &sock_len) < 0) {
      LOGE("Get socket info failed");
      break;
    }
  } while (0);

  if (ret < 0) {
    udp_socket_close(udp_socket);
    return -1;
  }

  switch (udp_socket->bind_addr.family) {
#if CONFIG_IPV6
    case AF_INET6:
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin6.sin6_port);
      break;
#endif
    case AF_INET:
    default:
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin.sin_port);
      break;
  }

#ifdef _XBOX
  {
    int socket_type = 0;
    socklen_t type_size = sizeof(socket_type);
    char local_ip[ADDRSTRLEN] = {0};
    int type_rc = getsockopt(udp_socket->fd, SOL_SOCKET, SO_TYPE,
                             (char*)&socket_type, &type_size);
    addr_to_string(&udp_socket->bind_addr, local_ip, sizeof(local_ip));
    LOGI("UDP socket ready fd=%d type_rc=%d type=%d local=%s:%u raw_addr=0x%08lx",
         udp_socket->fd, type_rc, socket_type, local_ip,
         (unsigned)udp_socket->bind_addr.port,
         udp_socket->bind_addr.sin.sin_addr.s_addr);
  }
#endif

  return 0;
}

void udp_socket_close(UdpSocket* udp_socket) {
  if (udp_socket->fd > 0) {
    close(udp_socket->fd);
  }
}

int udp_socket_sendto(UdpSocket* udp_socket, Address* addr, const uint8_t* buf, int len) {
  struct sockaddr* sa;
  socklen_t sock_len;
  int ret = -1;

  if (udp_socket->fd < 0) {
    LOGE("sendto before socket init");
    return -1;
  }

  switch (addr->family) {
#if CONFIG_IPV6
    case AF_INET6:
      addr->sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr*)&addr->sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
#endif
    case AF_INET:
    default:
      addr->sin.sin_family = AF_INET;
      sa = (struct sockaddr*)&addr->sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

#ifdef _XBOX
  /* On retail XNet, raw sendto() is incorrectly sent through the secure-peer
     route and returns WSAEHOSTUNREACH/LOST.  A connected UDP socket uses the
     title's authorized insecure Internet route, as the working TCP transport
     does.  UDP connect is local and may be repeated to select another ICE
     candidate. */
  if (!udp_socket->connected || !addr_equal(&udp_socket->connected_addr, addr)) {
    int connect_rc = connect(udp_socket->fd, sa, sock_len);
    int connect_error = connect_rc == 0 ? 0 : WSAGetLastError();
    LOGI("UDP connect fd=%d endpoint_raw=0x%08lx port=%u rc=%d wsa=%d",
         udp_socket->fd, addr->sin.sin_addr.s_addr, (unsigned)addr->port,
         connect_rc, connect_error);
    if (connect_rc == 0) {
      memcpy(&udp_socket->connected_addr, addr, sizeof(*addr));
      udp_socket->connected = 1;
    } else {
      udp_socket->connected = 0;
      return -1;
    }
  }
  ret = send(udp_socket->fd, (const char*)buf, len, 0);
  if (ret >= 0) return ret;
  {
    char endpoint[ADDRSTRLEN] = {0};
    int send_error = WSAGetLastError();
    DWORD route_before = XNetGetConnectStatus(addr->sin.sin_addr);
    int route_start = -1;
    DWORD route_after = route_before;
    addr_to_string(addr, endpoint, sizeof(endpoint));
    LOGE("Failed connected UDP send fd=%d endpoint=%s:%u sockaddr_port=0x%04x raw_addr=0x%08lx family=%u bytes=%d wsa=%d",
         udp_socket->fd, endpoint, (unsigned)addr->port,
         (unsigned)addr->sin.sin_port, addr->sin.sin_addr.s_addr,
         (unsigned)addr->family, len, send_error);
    /* A datagram send is documented to initiate an XNet route, but some
       retail kernels leave it idle.  Start it explicitly after a local
       no-route rejection and record the exact state transition. */
    if (send_error == WSAEHOSTUNREACH && route_before == XNET_CONNECT_STATUS_IDLE) {
      route_start = XNetConnect(addr->sin.sin_addr);
      route_after = XNetGetConnectStatus(addr->sin.sin_addr);
    }
    LOGE("XNet UDP route endpoint=%s:%u before=%lu connect_rc=%d after=%lu",
         endpoint, (unsigned)addr->port, (unsigned long)route_before,
         route_start, (unsigned long)route_after);
    return -1;
  }
#else
  if ((ret = sendto(udp_socket->fd, (const char*)buf, len, 0, sa, sock_len)) < 0) {
    LOGE("Failed to sendto: %s", strerror(errno));
    return -1;
  }
#endif

  return ret;
}

int udp_socket_recvfrom(UdpSocket* udp_socket, Address* addr, uint8_t* buf, int len) {
#if CONFIG_IPV6
  struct sockaddr_in6 sin6;
#endif
  struct sockaddr_in sin;
  struct sockaddr* sa;
  socklen_t sock_len;
  int ret;

  if (udp_socket->fd < 0) {
    LOGE("recvfrom before socket init");
    return -1;
  }

  switch (udp_socket->bind_addr.family) {
#if CONFIG_IPV6
    case AF_INET6:
      sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr*)&sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
#endif
    case AF_INET:
    default:
      sin.sin_family = AF_INET;
      sa = (struct sockaddr*)&sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  if ((ret = recvfrom(udp_socket->fd, (char*)buf, len, 0, sa, &sock_len)) < 0) {
    LOGE("Failed to recvfrom: %s", strerror(errno));
    return -1;
  }

  if (addr) {
    switch (udp_socket->bind_addr.family) {
#if CONFIG_IPV6
      case AF_INET6:
        addr->family = AF_INET6;
        addr->port = htons(sin6.sin6_port);
        memcpy(&addr->sin6, &sin6, sizeof(struct sockaddr_in6));
        break;
#endif
      case AF_INET:
      default:
        addr->family = AF_INET;
        addr->port = ntohs(sin.sin_port);
        memcpy(&addr->sin, &sin, sizeof(struct sockaddr_in));
        break;
    }
  }

  return ret;
}

int tcp_socket_open(TcpSocket* tcp_socket, int family) {
  tcp_socket->bind_addr.family = family;
  switch (family) {
#if CONFIG_IPV6
    case AF_INET6:
      tcp_socket->fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
      break;
#endif
    case AF_INET:
    default:
      tcp_socket->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      break;
  }

  if (tcp_socket->fd < 0) {
    LOGE("Failed to create socket");
    return -1;
  }
  return 0;
}

int tcp_socket_connect(TcpSocket* tcp_socket, Address* addr) {
  char addr_string[ADDRSTRLEN];
  int ret;
  struct sockaddr* sa;
  socklen_t sock_len;

  if (tcp_socket->fd < 0) {
    LOGE("Connect before socket init");
    return -1;
  }

  switch (addr->family) {
#if CONFIG_IPV6
    case AF_INET6:
      addr->sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr*)&addr->sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
#endif
    case AF_INET:
    default:
      addr->sin.sin_family = AF_INET;
      sa = (struct sockaddr*)&addr->sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  addr_to_string(addr, addr_string, sizeof(addr_string));
  LOGI("Connecting to server: %s:%d", addr_string, addr->port);
  if ((ret = connect(tcp_socket->fd, sa, sock_len)) < 0) {
    LOGE("Failed to connect to server");
    return -1;
  }

  LOGI("Server is connected");
  return 0;
}

void tcp_socket_close(TcpSocket* tcp_socket) {
  if (tcp_socket->fd > 0) {
    close(tcp_socket->fd);
  }
}

int tcp_socket_send(TcpSocket* tcp_socket, const uint8_t* buf, int len) {
  int ret;

  if (tcp_socket->fd < 0) {
    LOGE("sendto before socket init");
    return -1;
  }

  ret = send(tcp_socket->fd, (const char*)buf, len, 0);
  if (ret < 0) {
    LOGE("Failed to send: %s", strerror(errno));
    return -1;
  }
  return ret;
}

int tcp_socket_recv(TcpSocket* tcp_socket, uint8_t* buf, int len) {
  int ret;

  if (tcp_socket->fd < 0) {
    LOGE("recvfrom before socket init");
    return -1;
  }

  ret = recv(tcp_socket->fd, (char*)buf, len, 0);
  if (ret < 0) {
    LOGE("Failed to recv: %s", strerror(errno));
    return -1;
  }
  return ret;
}
