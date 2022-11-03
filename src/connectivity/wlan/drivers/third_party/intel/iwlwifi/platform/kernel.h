// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_KERNEL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_KERNEL_H_

// This file contains Fuchsia-specific kernel support code, including those kernel structs and
// routines that are typically provided by the Linux kernel API.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <limits.h>
#include <netinet/if_ether.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/common.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

typedef uint32_t netdev_features_t;

typedef uint64_t dma_addr_t;

typedef char* acpi_string;

#define ETHTOOL_FWVERS_LEN 32

#define IS_ENABLED(x) (false)

#define iwl_assert_lock_held(x) ZX_DEBUG_ASSERT(mtx_trylock(x) == thrd_busy)

// NEEDS_TYPES: how to guarantee this?
#define READ_ONCE(x) (x)

#define DMA_BIT_MASK(n) (((n) >= 64) ? ~0ULL : ((1ULL << (n)) - 1))

// Converts a WiFi time unit (1024 us) to zx_duration_t.
//
// "IEEE Std 802.11-2007" 2007-06-12. p. 14. Retrieved 2010-07-20.
// time unit (TU): A measurement of time equal to 1024 μs.
//
#define TU_TO_DURATION(time_unit) (ZX_USEC(1024) * time_unit)

struct zx_device;
struct async_dispatcher;
struct driver_inspector;
struct rcu_manager;

// This struct is analogous to the Linux device struct, and contains all the Fuchsia-specific data
// fields relevant to generic device functionality.
struct device {
  // The zx_device of the base iwlwifi Device.
  struct zx_device* zxdev;

  // The BTI handle used to map IO buffers for this device.
  zx_handle_t bti;

  // The dispatcher used to dispatch work queue tasks, equivalent to the Linux workqueue.  On Linux,
  // these are run in process context, in contrast with timer tasks that are run in interrupt
  // context.  Fuchsia drivers have no separate interrupt context, but to maintain similar
  // performance characteristics we will maintain a dedicated work queue dispatcher here.
  struct async_dispatcher* task_dispatcher;

  // The dispatcher used to dispatch IRQ tasks.  On Linux, these are run in interrupt context.
  // Fuchsia has no separate interrupt context, but to maintain similar performance characteristics
  // we will maintain a dedicated IRQ dispatcher here.
  struct async_dispatcher* irq_dispatcher;

  // The RCU manager instance used to manage RCU-based synchronization.
  struct rcu_manager* rcu_manager;

  // The inspector used to publish the component inspection tree from the driver.
  struct driver_inspector* inspector;
};

// This struct is analogous to the Linux pci_device_id struct, and contains PCI device ID values.
struct iwl_pci_device_id {
  uint16_t device_id;
  uint16_t subsystem_device_id;
  const struct iwl_cfg* config;
};

// An opaque struct that hides a C++ FIDL client, allowing us to use FIDL
// without rewriting the whole driver in C++.
struct iwl_pci_fidl;

// This struct is analogous to the Linux pci_dev struct, and contans Fuchsia-specific PCI bus
// interface data.
struct iwl_pci_dev {
  struct device dev;
  struct iwl_pci_fidl* fidl;
  struct iwl_trans* drvdata;
};

// This struct holds a VMO reference to a firmware binary.
struct firmware {
  zx_handle_t vmo;
  uint8_t* data;
  size_t size;
};

struct page;

// NEEDS_TYPES: Below structures are only referenced in function prototype.
//                Doesn't need a dummy byte.
struct dentry;
struct wait_queue;
struct wiphy;

// NEEDS_TYPES: Below structures are used in code but not ported yet.
// A dummy byte is required to suppress the C++ warning message for empty
// struct.

struct work_struct {
  char dummy;
};

struct delayed_work {
  struct work_struct work;
};

struct ewma_rate {
  char dummy;
};

struct inet6_dev;

struct mac_address {
  uint8_t addr[ETH_ALEN];
};

struct napi_struct {
  char dummy;
};

struct rcu_head {
  char dummy;
};

struct sk_buff_head {
  char dummy;
};

struct wait_queue_head {
  char dummy;
};

struct wireless_dev {
  wlan_mac_role_t iftype;
};

////
// Typedefs
////
typedef struct wait_queue wait_queue_t;
typedef struct wait_queue_head wait_queue_head_t;

static inline void* vmalloc(unsigned long size) { return malloc(size); }

static inline void* kmemdup(const void* src, size_t len) {
  ZX_ASSERT(src);
  void* dst = malloc(len);
  if (dst) {
    memcpy(dst, src, len);
  }
  return dst;
}

static inline void vfree(const void* addr) { free((void*)addr); }

static inline void kfree(void* addr) { free(addr); }

static inline bool IS_ERR_OR_NULL(const void* ptr) {
  return !ptr || (((unsigned long)ptr) >= (unsigned long)-4095);
}

static inline void list_splice_after_tail(list_node_t* splice_from, list_node_t* pos) {
  if (list_is_empty(pos)) {
    list_move(splice_from, pos);
  } else {
    list_splice_after(splice_from, list_peek_tail(pos));
  }
}

// The following MAC addresses are invalid addresses.
//
//   00:00:00:00:00:00
//   *1:**:**:**:**:**  (multicast: the LSB of first byte is 1)
//   FF:FF:FF:FF:FF:FF  (also a nulticast address)
static inline bool is_valid_ether_addr(const uint8_t* mac) {
  return !((!mac[0] && !mac[1] && !mac[2] && !mac[3] && !mac[4] && !mac[5]) ||  // 00:00:00:00:00:00
           (mac[0] & 1));                                                       // multicast
}

// Fill the MAC address with broadcast address (all-0xff).
//
static inline void eth_broadcast_addr(uint8_t* addr) {
  for (size_t i = 0; i < ETH_ALEN; i++) {
    addr[i] = 0xff;
  }
}

static inline bool is_broadcast_addr(const uint8_t* mac) {
  uint8_t bcast[ETH_ALEN];
  eth_broadcast_addr(bcast);
  return !memcmp(bcast, mac, ETH_ALEN);
}

static inline bool is_multicast_addr(const uint8_t* mac) { return mac[0] & 1; }

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_KERNEL_H_
