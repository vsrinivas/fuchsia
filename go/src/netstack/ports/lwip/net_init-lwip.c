// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/lwip/src/include/lwip/dhcp.h"
#include "third_party/lwip/src/include/lwip/dns.h"
#include "third_party/lwip/src/include/lwip/inet.h"
#include "third_party/lwip/src/include/lwip/netif.h"
#include "third_party/lwip/src/include/lwip/netifapi.h"
#include "third_party/lwip/src/include/lwip/sockets.h"
#include "third_party/lwip/src/include/lwip/stats.h"
#include "third_party/lwip/src/include/lwip/tcpip.h"
#if USE_LWIPERF
#include "third_party/lwip/src/include/lwip/apps/lwiperf.h"
#endif

#include "apps/netstack/net_init.h"
#include "apps/netstack/trace.h"
#include "apps/netstack/ports/lwip/ethernetif.h"

typedef struct {
  char name[16];  // null-terminated
  struct sockaddr addr;
  struct sockaddr netmask;
  struct sockaddr broadaddr;
  uint32_t flags;
  uint16_t index;
  uint16_t hwaddr_len;
  uint8_t hwaddr[8];
} lwip_net_if_info_t;

// flags
#define IFF_UP 0x1

static struct netif s_netif;

static int ip6_addr_printed_bits = 0;

static struct {
  ip4_addr_t ip_addr;
  ip4_addr_t netmask;
  ip4_addr_t gateway;
} current_addrs;
static int current_dhcp_status;

static void get_current_addrs_callback(struct netif* netif) {
  memcpy(&current_addrs.ip_addr, &netif->ip_addr, sizeof(ip4_addr_t));
  memcpy(&current_addrs.netmask, &netif->netmask, sizeof(ip4_addr_t));
  memcpy(&current_addrs.gateway, &netif->gw, sizeof(ip4_addr_t));
}

static void print_ip4_addrs(struct netif* netif) {
  if (!ip_addr_isany(&netif->ip_addr)) {
    if (memcmp(&current_addrs.ip_addr, &netif->ip_addr, sizeof(ip4_addr_t)) ||
        memcmp(&current_addrs.netmask, &netif->netmask, sizeof(ip4_addr_t))) {
      char ip_addr[INET_ADDRSTRLEN];
      char netmask[INET_ADDRSTRLEN];
      ipaddr_ntoa_r(&netif->ip_addr, ip_addr, sizeof(ip_addr));
      ipaddr_ntoa_r(&netif->netmask, netmask, sizeof(netmask));
      info("ip4_addr: %s netmask: %s\n", ip_addr, netmask);
    }
    if (memcmp(&current_addrs.gateway, &netif->gw, sizeof(ip4_addr_t))) {
      char gw[INET_ADDRSTRLEN];
      ipaddr_ntoa_r(&netif->gw, gw, sizeof(gw));
      info("gw: %s\n", gw);
    }
  }
  get_current_addrs_callback(netif);
}

static void print_ip6_addr(struct netif* netif, int *printed_bits, int idx) {
  if (netif->ip6_addr_state[idx] & IP6_ADDR_PREFERRED) {
    char ip6_addr[INET6_ADDRSTRLEN];
    info("ip6_addr[%d]: %s\n", idx,
         ip6addr_ntoa_r(netif_ip6_addr(netif, idx), ip6_addr,
                        sizeof(ip6_addr)));
    *printed_bits = (1 << idx);
  }
}

// the callback called every time the netif status changes
static void lwip_netif_status_callback(struct netif *netif) {
  print_ip4_addrs(netif);

  for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
    if (!(ip6_addr_printed_bits & (1 << i)))
      print_ip6_addr(netif, &ip6_addr_printed_bits, i);
  }
}

#if USE_LWIPERF
// the callback called for lwiperf reports
static void lwip_iperf_report(void *arg, enum lwiperf_report_type report_type,
                              const ip_addr_t *local_addr, u16_t local_port,
                              const ip_addr_t *remote_addr, u16_t remote_port,
                              u32_t bytes_transferred, u32_t ms_duration,
                              u32_t bandwidth_kbitpsec) {
  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(local_addr);
  LWIP_UNUSED_ARG(local_port);

  printf(
      "iperf report [%d]: %s:%u, transferred: %u (bytes), duration: %u (ms), "
      "bandwidth %u (kb/s)\n",
      report_type, ipaddr_ntoa(remote_addr), remote_port, bytes_transferred,
      ms_duration, bandwidth_kbitpsec);
}
#endif

// the callback to signal sem_tcpip_done
static void lwip_tcpip_init_done(void *arg) {
  sys_sem_t *sem;
  sem = (sys_sem_t *)arg;
  sys_sem_signal(sem);
}

static int lwip_netif_init(void) {
  sys_sem_t sem_tcpip_done;

  // initialize tcpip in lwip

  // create a semaphore to wait for tcpip initialization
  if (sys_sem_new(&sem_tcpip_done, 0) != ERR_OK) {
    LWIP_ASSERT("failed to create semaphore", 0);
  }
  // pass lwip_tcpip_init_done as the callback
  tcpip_init(lwip_tcpip_init_done, &sem_tcpip_done);
  // block until the semaphore is signaled by lwip_tcp_init_done
  sys_sem_wait(&sem_tcpip_done);
  // free the semaphore
  sys_sem_free(&sem_tcpip_done);

  // create an network interface and run dhcp

  // add an interface without passing ip_address/netmask/gateway
  netif_add(&s_netif, NULL, NULL, NULL, NULL, ethernetif_init, tcpip_input);
  // make it the default interface
  netif_set_default(&s_netif);
  // set a callback to detect when IP address is assgined by dhcp
  netif_set_status_callback(&s_netif, lwip_netif_status_callback);

  // turn on the interface
  netif_set_up(&s_netif);

  // create ipv6 linklocal address
  netif_create_ip6_linklocal_address(&s_netif, 1);

  // start dhcp
  dhcp_start(&s_netif);
  current_dhcp_status = 1;

#if USE_LWIPERF
  // start iperf server
  lwiperf_start_tcp_server_default(lwip_iperf_report, NULL);
#endif

  return 0;
}

int net_init(void) {
#if defined(NETSTACK_DEBUG)
  lwip_debug_flags = LWIP_DBG_ON;
#endif
  return lwip_netif_init();
}

static void lwip_netif_set_status_callback(struct netif* netif) {
  netif_set_status_callback(netif, lwip_netif_status_callback);
}

static void lwip_netif_create_ip6_linklocal_address(struct netif* netif) {
  netif_create_ip6_linklocal_address(netif, 1);
}

int net_reinit(void) {
  netifapi_dhcp_stop(&s_netif);
  netifapi_netif_set_down(&s_netif);
  netifapi_netif_remove(&s_netif);
  mem_free(s_netif.state);

  ip6_addr_printed_bits = 0;
  netifapi_netif_add(&s_netif, NULL, NULL, NULL, NULL, ethernetif_init,
                     tcpip_input);
  netifapi_netif_common(&s_netif, lwip_netif_set_status_callback, NULL);
  netifapi_netif_set_default(&s_netif);
  netifapi_netif_set_up(&s_netif);
  netifapi_netif_common(&s_netif, lwip_netif_create_ip6_linklocal_address, NULL);
  netifapi_dhcp_start(&s_netif);
  current_dhcp_status = 1;

  return 0;
}

void net_debug(void) {
  stats_display();
}

static void ip4addr_to_sockaddr_in(const ip4_addr_t* ip4addr,
                                   struct sockaddr_in* sin) {
  sin->sin_len = sizeof(struct sockaddr_in);
  sin->sin_family = AF_INET;
  sin->sin_port = lwip_htons(0);
  inet_addr_from_ip4addr(&sin->sin_addr, ip4addr);
  memset(sin->sin_zero, 0, SIN_ZERO_LEN);
}

static void sockaddr_in_to_ip4addr(const struct sockaddr_in* sin,
                                   ip4_addr_t* ip4addr) {
  inet_addr_to_ip4addr(ip4addr, &(sin->sin_addr));
}

int lwip_net_get_if_info(int index, lwip_net_if_info_t* info) {
  if (index > 0)
    return 0;

  strcpy(info->name, "en0");
  netifapi_netif_common(&s_netif, get_current_addrs_callback, NULL);
  ip4addr_to_sockaddr_in(&current_addrs.ip_addr,
                         (struct sockaddr_in*)&info->addr);
  ip4addr_to_sockaddr_in(&current_addrs.netmask,
                         (struct sockaddr_in*)&info->netmask);
  ip4_addr_t broadaddr;
  broadaddr.addr = current_addrs.ip_addr.addr | ~current_addrs.netmask.addr;
  ip4addr_to_sockaddr_in(&broadaddr, (struct sockaddr_in*)&info->broadaddr);
  // TODO: support more flags
  info->flags = netif_is_up(&s_netif) ? IFF_UP : 0;
  info->index = 0;
  info->hwaddr_len = s_netif.hwaddr_len;
  memcpy(info->hwaddr, s_netif.hwaddr, s_netif.hwaddr_len);
  return 1;
}

int lwip_net_set_if_addr_v4(const char* ifname,
                            const struct sockaddr* ipaddr,
                            const struct sockaddr* netmask) {
  if (strcmp(ifname, "en0"))
    return -1;
  if (ipaddr->sa_family != AF_INET || netmask->sa_family != AF_INET)
    return -1;
  netifapi_netif_common(&s_netif, get_current_addrs_callback, NULL);

  ip4_addr_t ip4_ipaddr;
  ip4_addr_t ip4_netmask;
  sockaddr_in_to_ip4addr((const struct sockaddr_in*)ipaddr, &ip4_ipaddr);
  sockaddr_in_to_ip4addr((const struct sockaddr_in*)netmask, &ip4_netmask);
  netifapi_netif_set_addr(&s_netif, &ip4_ipaddr, &ip4_netmask,
                          &current_addrs.gateway);
  return 0;
}

int lwip_net_get_if_gateway_v4(const char* ifname, struct sockaddr* gateway) {
  if (strcmp(ifname, "en0"))
    return -1;
  netifapi_netif_common(&s_netif, get_current_addrs_callback, NULL);
  ip4addr_to_sockaddr_in(&current_addrs.gateway, (struct sockaddr_in*)gateway);
  return 0;
}

int lwip_net_set_if_gateway_v4(const char* ifname,
                               const struct sockaddr* gateway) {
  if (strcmp(ifname, "en0"))
    return -1;
  if (gateway->sa_family != AF_INET)
    return -1;
  netifapi_netif_common(&s_netif, get_current_addrs_callback, NULL);

  ip4_addr_t ip4_gateway;
  sockaddr_in_to_ip4addr((const struct sockaddr_in*)gateway, &ip4_gateway);
  netifapi_netif_set_addr(&s_netif, &current_addrs.ip_addr,
                          &current_addrs.netmask, &ip4_gateway);
  return 0;
}

int lwip_net_get_dhcp_status_v4(const char* ifname, int* dhcp_status) {
  if (strcmp(ifname, "en0"))
    return -1;
  *dhcp_status = current_dhcp_status;
  return 0;
}

int lwip_net_set_dhcp_status_v4(const char* ifname, const int dhcp_status) {
  if (strcmp(ifname, "en0"))
    return -1;
  if (current_dhcp_status != !!dhcp_status) {
    if (dhcp_status)
      netifapi_dhcp_start(&s_netif);
    else
      netifapi_dhcp_stop(&s_netif);
    current_dhcp_status = !!dhcp_status;
  }
  return 0;
}

int lwip_net_get_dns_server_v4(struct sockaddr* dns_server) {
  const ip_addr_t* server = dns_getserver(0);
  // TODO: ipv6
  if (!IP_IS_V4(server))
    return -1;
  ip4addr_to_sockaddr_in(ip_2_ip4(server), (struct sockaddr_in*)dns_server);
  return 0;
}

int lwip_net_set_dns_server_v4(const struct sockaddr* dns_server) {
  // TODO: ipv6
  if (dns_server->sa_family != AF_INET)
    return -1;
  ip_addr_t server;
  sockaddr_in_to_ip4addr((struct sockaddr_in*)dns_server, ip_2_ip4(&server));
  IP_SET_TYPE_VAL(server, IPADDR_TYPE_V4);
  dns_setserver(0, &server);
  return 0;
}
