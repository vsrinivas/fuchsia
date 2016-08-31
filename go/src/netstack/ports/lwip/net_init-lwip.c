// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/lwip/ports/fuchsia/include/netif/ethernetif.h"
#include "third_party/lwip/src/include/lwip/dhcp.h"
#include "third_party/lwip/src/include/lwip/netif.h"
#include "third_party/lwip/src/include/lwip/tcpip.h"

// initialize a lwip network interface

static struct netif netif;
static sys_mutex_t mutex_netif_status;
static sys_sem_t sem_dhcp;

// the callback to signal sem_tcpip_done
static void lwip_tcpip_init_done(void *arg) {
  sys_sem_t *sem;
  sem = (sys_sem_t *)arg;
  sys_sem_signal(sem);
}

static int lwip_netif_has_ip4_addr(void) {
  return !ip_addr_isany(&netif.ip_addr);
}

// the callback to signal sem_dhcp
static void lwip_netif_status_callback(struct netif *netif) {
  sys_mutex_lock(&mutex_netif_status);
  if (sys_sem_valid(&sem_dhcp) && lwip_netif_has_ip4_addr())
    sys_sem_signal(&sem_dhcp);
  sys_mutex_unlock(&mutex_netif_status);
}

static int lwip_netif_init(unsigned int timeout_in_msec) {
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

  // create a mutex to access netif data
  sys_mutex_new(&mutex_netif_status);
  // add an interface without passing ip_address/netmask/gateway
  netif_add(&netif, NULL, NULL, NULL, NULL, ethernetif_init, tcpip_input);
  // make it the default interface
  netif_set_default(&netif);
  // set a callback to detect when IP address is assgined by dhcp
  netif_set_status_callback(&netif, lwip_netif_status_callback);

  // turn on the interface
  netif_set_up(&netif);
  netif_create_ip6_linklocal_address(&netif, 1);

  // create a semaphore to wait for dhcp
  if (sys_sem_new(&sem_dhcp, 0) != ERR_OK) {
    LWIP_ASSERT("failed to create semaphore", 0);
  }
  // start dhcp
  // TODO: handle lease renewal
  dhcp_start(&netif);

  // block until an IP address is assigned by dhcp
  // will timeout if an IP address is not assigned after 'timeout_in_msec'
  // milliseconds
  int ret = 0;
  if (sys_arch_sem_wait(&sem_dhcp, timeout_in_msec) == SYS_ARCH_TIMEOUT)
    ret = -1;

  sys_mutex_lock(&mutex_netif_status);
  // free the semaphore
  sys_sem_free(&sem_dhcp);
  // invalid the semaphore (see lwip_netif_status_changed() checks this)
  sys_sem_set_invalid(&sem_dhcp);
  sys_mutex_unlock(&mutex_netif_status);

  return ret;
}

int net_init(void) {
#if defined(NETSTACK_DEBUG)
  lwip_debug_flags = LWIP_DBG_ON;
#endif
  int ret = lwip_netif_init(5000);
  if (ret == 0) {
    // IP address is assigned by dhcp
    char ip4_addr[IPADDR_STRLEN_MAX];
    ipaddr_ntoa_r(&netif.ip_addr, ip4_addr, IPADDR_STRLEN_MAX);
    printf("netstack: ip4_addr: %s\n", ip4_addr);
  }
  return ret;
}
