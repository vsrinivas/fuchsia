// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <sys/socket.h>

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

__BEGIN_CDECLS

#define NETC_IFNAME_SIZE 16
#define NETC_HWADDR_SIZE 8
#define NETC_IF_INFO_MAX 16

typedef struct {
  char name[NETC_IFNAME_SIZE]; // null-terminated
  struct sockaddr_storage addr;
  struct sockaddr_storage netmask;
  struct sockaddr_storage broadaddr;
  uint32_t flags;
  uint16_t index;
  uint16_t hwaddr_len;
  uint8_t hwaddr[NETC_HWADDR_SIZE];
} netc_if_info_t;

#define NETC_IFF_UP 0x1

typedef struct {
  uint32_t n_info;
  netc_if_info_t info[NETC_IF_INFO_MAX];
} netc_get_if_info_t;

typedef struct {
  char name[NETC_IFNAME_SIZE]; // null-terminated
  struct sockaddr_storage addr;
  struct sockaddr_storage netmask;
} netc_set_if_addr_t;

typedef struct {
  char name[NETC_IFNAME_SIZE]; // null-terminated
  struct sockaddr_storage gateway;
} netc_set_if_gateway_t;

typedef struct {
  char name[NETC_IFNAME_SIZE]; // null-terminated
  int status; // 1: running, 0: not running
} netc_set_dhcp_status_t;

typedef struct {
  struct sockaddr_storage dns_server;
} netc_set_dns_server_t;

#define IOCTL_FAMILY_NETCONFIG 0xff

#define IOCTL_NETC_GET_IF_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NETCONFIG, 0)
#define IOCTL_NETC_SET_IF_ADDR \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NETCONFIG, 1)
#define IOCTL_NETC_GET_IF_GATEWAY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NETCONFIG, 2)
#define IOCTL_NETC_SET_IF_GATEWAY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NETCONFIG, 3)
#define IOCTL_NETC_GET_DHCP_STATUS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NETCONFIG, 4)
#define IOCTL_NETC_SET_DHCP_STATUS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NETCONFIG, 5)
#define IOCTL_NETC_GET_DNS_SERVER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NETCONFIG, 6)
#define IOCTL_NETC_SET_DNS_SERVER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NETCONFIG, 7)

// Get if info
// ssize_t ioctl_netc_get_if_info(int fd, netc_get_if_info_t* get_if_info)
IOCTL_WRAPPER_OUT(ioctl_netc_get_if_info, IOCTL_NETC_GET_IF_INFO, netc_get_if_info_t);

// Set if addr
// ssize_t ioctl_netc_set_if_addr(int fd, netc_set_if_addr_t* set_if_addr)
IOCTL_WRAPPER_IN(ioctl_netc_set_if_addr, IOCTL_NETC_SET_IF_ADDR, netc_set_if_addr_t);

// Get if gateway
static inline ssize_t ioctl_netc_get_if_gateway(int fd, char* name, struct sockaddr_storage* ss) {
  return mxio_ioctl(fd, IOCTL_NETC_GET_IF_GATEWAY, name, NETC_IFNAME_SIZE,
                    ss, sizeof(struct sockaddr_storage));
}

// Set if gateway
// ssize_t ioctl_netc_set_if_gateway(int fd, netc_set_if_gateway_t* set_if_gateway)
IOCTL_WRAPPER_IN(ioctl_netc_set_if_gateway, IOCTL_NETC_SET_IF_GATEWAY, netc_set_if_gateway_t);

// Get DHCP status
static inline ssize_t ioctl_netc_get_dhcp_status(int fd, char* name, int* status) {
  return mxio_ioctl(fd, IOCTL_NETC_GET_DHCP_STATUS, name, NETC_IFNAME_SIZE,
                    status, sizeof(int));
}

// Set DHCP status
// ssize_t ioctl_netc_set_dhcp_status(int fd, netc_set_dhcp_status_t* set_dhcp_status)
IOCTL_WRAPPER_IN(ioctl_netc_set_dhcp_status, IOCTL_NETC_SET_DHCP_STATUS, netc_set_dhcp_status_t);

// Get DNS Server
// ssize_t ioctl_netc_get_dns_server(int fd, struct sockaddr_storage* dns_server)
IOCTL_WRAPPER_OUT(ioctl_netc_get_dns_server, IOCTL_NETC_GET_DNS_SERVER, struct sockaddr_storage);

// Set DNS Server
// ssize_t ioctl_netc_set_dns_server(int fd, struct sockaddr_storage* dns_server)
IOCTL_WRAPPER_IN(ioctl_netc_set_dns_server, IOCTL_NETC_SET_DNS_SERVER, struct sockaddr_storage);

__END_CDECLS
