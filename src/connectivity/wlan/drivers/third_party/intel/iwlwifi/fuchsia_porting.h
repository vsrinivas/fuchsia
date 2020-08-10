// Copyright (c) 2019 The Fuchsia Authors.
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_PORTING_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_PORTING_H_

#include <lib/backtrace-request/backtrace-request.h>
#include <netinet/if_ether.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <ddk/hw/wlan/wlaninfo.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/ieee80211.h"

typedef uint32_t __be32;
typedef uint16_t __be16;
typedef uint64_t __le64;
typedef uint32_t __le32;
typedef uint16_t __le16;
typedef uint8_t __s8;
typedef uint8_t __u8;

typedef uint32_t netdev_features_t;

typedef uint64_t dma_addr_t;

typedef char* acpi_string;

#define BIT(x) (1 << (x))

#define DIV_ROUND_UP(num, div) (((num) + (div)-1) / (div))

#define be16_to_cpup(x) ((*(x) >> 8) | ((*(x)&0xff) << 8))
#define le64_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le32_to_cpup(x) (*(x))
#define le16_to_cpu(x) (x)
#define le16_to_cpup(x) (*(x))
#define cpu_to_le64(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le16(x) (x)

#define lower_32_bits(x) (x & 0xffffffff)
#define upper_32_bits(x) (x >> 32)

#define BITS_PER_BYTE 8
#define BITS_PER_INT (sizeof(int) * BITS_PER_BYTE)
#define BITS_PER_LONG (sizeof(long) * BITS_PER_BYTE)

#define BITS_TO_INTS(nr) (DIV_ROUND_UP(nr, BITS_PER_INT))
#define BITS_TO_LONGS(nr) (DIV_ROUND_UP(nr, BITS_PER_LONG))

#define ETHTOOL_FWVERS_LEN 32

#define IS_ENABLED(x) (false)

#define iwl_assert_lock_held(x) ZX_DEBUG_ASSERT(mtx_trylock(x) == thrd_busy)

#define __aligned(x) __attribute__((aligned(x)))
#define __force
#define __must_check __attribute__((warn_unused_result))
#define __packed __PACKED
#define __rcu  // NEEDS_PORTING
#define ____cacheline_aligned_in_smp  // NEEDS_PORTING

// NEEDS_PORTING: Need to check if 'x' is static array.
#define ARRAY_SIZE(x) (countof(x))

// NEEDS_PORTING
#define WARN(x, y, z...) \
  do {                   \
  } while (0)
#define WARN_ON(x) (!!(x))
#define WARN_ON_ONCE(x) (!!(x))
#define BUILD_BUG_ON(x) ZX_ASSERT(!(x))

#define offsetofend(type, member) (offsetof(type, member) + sizeof(((type*)NULL)->member))

// NEEDS_PORTING: need to be generic
// clang-format off
#define roundup_pow_of_two(x)  \
  (x >= 0x100 ? 0xFFFFFFFF :   \
   x >= 0x080 ? 0x100 :        \
   x >= 0x040 ? 0x080 :        \
   x >= 0x020 ? 0x040 :        \
   x >= 0x010 ? 0x020 :        \
   x >= 0x008 ? 0x010 :        \
   x >= 0x004 ? 0x008 :        \
   x >= 0x002 ? 0x004 :        \
   x >= 0x001 ? 0x002 : 1)
// clang-format on
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define ROUND_UP(x, y) ((((x)-1) | __round_mask(x, y)) + 1)
#define ROUND_DOWN(x, y) ((x) & ~__round_mask(x, y))

// NEEDS_PORTING: need protection while accessing the variable.
#define rcu_dereference(p) (p)
#define rcu_dereference_protected(p, c) (p)

// NEEDS_PORTING: how to guarantee this?
#define READ_ONCE(x) (x)

// NEEDS_PORTING: implement this
#define rcu_read_lock() \
  do {                  \
  } while (0);
#define rcu_read_unlock() \
  do {                    \
  } while (0);

#define DMA_BIT_MASK(n) (((n) >= 64) ? ~0ULL : ((1ULL << (n)) - 1))

// Converts a WiFi time unit (1024 us) to zx_duration_t.
//
// "IEEE Std 802.11-2007" 2007-06-12. p. 14. Retrieved 2010-07-20.
// time unit (TU): A measurement of time equal to 1024 μs.
//
#define TU_TO_DURATION(time_unit) (ZX_USEC(1024) * time_unit)

// Returns the current time.
//
// Args:
//   dispatcher: dispatcher in async. Usually the mvm->dispatcher.
//
// Returns:
//   zx_time_t
//
#define NOW_TIME(dispatcher) (async_now(dispatcher))

// Returns the expiring time.
//
// Args:
//   dispatcher: dispatcher in async. Usually the mvm->dispatcher.
//   time_unit: in TU (time unit)
//
// Returns:
//   zx_time_t
//
#define TU_TO_EXP_TIME(dispatcher, time_unit) \
  zx_time_add_duration(NOW_TIME(dispatcher), TU_TO_DURATION(time_unit))

// NEEDS_PORTING: Below structures are only referenced in function prototype.
//                Doesn't need a dummy byte.
struct dentry;
struct device;
struct wait_queue;
struct wiphy;

// NEEDS_PORTING: Below structures are used in code but not ported yet.
// A dummy byte is required to suppress the C++ warning message for empty
// struct.
struct delayed_work {
  char dummy;
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

struct timer_list {
  char dummy;
};

struct wait_queue_head {
  char dummy;
};

struct work_struct {
  char dummy;
};

struct firmware {
  zx_handle_t vmo;
  uint8_t* data;
  size_t size;
};

struct page {
  void* virtual_addr;
};

struct wireless_dev {
  wlan_info_mac_role_t iftype;
};

////
// Typedefs
////
typedef struct wait_queue wait_queue_t;
typedef struct wait_queue_head wait_queue_head_t;

typedef struct {
  int value;
} atomic_t;

////
// Inline functions
////
static inline int test_bit(int nbits, const volatile unsigned long* addr) {
  return 1UL & (addr[nbits / BITS_PER_LONG] >> (nbits % BITS_PER_LONG));
}

static inline bool test_and_set_bit(long bit, volatile unsigned long* addr) {
  unsigned long mask = 1ul << (bit % BITS_PER_LONG);
  return mask & __atomic_fetch_or(&addr[bit / BITS_PER_LONG], mask, __ATOMIC_SEQ_CST);
}

static inline bool test_and_clear_bit(long bit, volatile unsigned long* addr) {
  unsigned long mask = 1ul << (bit % BITS_PER_LONG);
  return mask & __atomic_fetch_and(&addr[bit / BITS_PER_LONG], ~mask, __ATOMIC_SEQ_CST);
}

static inline void set_bit(long bit, unsigned long* addr) { test_and_set_bit(bit, addr); }

static inline void clear_bit(long bit, volatile unsigned long* addr) {
  test_and_clear_bit(bit, addr);
}

// This is the non-atomic version of set_bit.
static inline void __set_bit(long bit, unsigned long* addr) {
  addr[bit / BITS_PER_LONG] |= 1ul << (bit % BITS_PER_LONG);
}

static inline int atomic_read(const atomic_t* atomic) {
  return __atomic_load_n(&atomic->value, __ATOMIC_RELAXED);
}

static inline void atomic_set(atomic_t* atomic, int value) {
  __atomic_store_n(&atomic->value, value, __ATOMIC_RELAXED);
}

static inline int atomic_xchg(atomic_t* atomic, int value) {
  return __atomic_exchange_n(&atomic->value, value, __ATOMIC_SEQ_CST);
}

static inline int atomic_inc(atomic_t* atomic) {
  return __atomic_fetch_add(&atomic->value, 1, __ATOMIC_SEQ_CST);
}

static inline int atomic_dec_if_positive(atomic_t* atomic) {
  int current = atomic_read(atomic);
  while (1) {
    if (current <= 0) {
      return current;
    }
    if (__atomic_compare_exchange_n(&atomic->value, &current, current - 1, false /* weak */,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      return current - 1;
    }
  }
}

static inline void* vmalloc(unsigned long size) { return malloc(size); }

static inline void* kmemdup(const void* src, size_t len) {
  void* dst = malloc(len);
  memcpy(dst, src, len);
  return dst;
}

static inline void vfree(const void* addr) { free((void*)addr); }

static inline void kfree(void* addr) { free(addr); }

static inline bool IS_ERR_OR_NULL(const void* ptr) {
  return !ptr || (((unsigned long)ptr) >= (unsigned long)-4095);
}

static inline void* page_address(const struct page* page) { return page->virtual_addr; }

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define max_t(type, a, b) MAX((type)(a), (type)(b))
#define min_t(type, a, b) MIN((type)(a), (type)(b))

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

// Find the first asserted LSB.
//
// Returns:
//   [0, num_bits): found. The index of first asserted bit (the least significant one.
//   num_bits: No asserted bit found in num_bits.
//
static inline size_t find_first_bit(unsigned* bits, const size_t num_bits) {
  const size_t num_of_ints = DIV_ROUND_UP(num_bits, BITS_PER_INT);
  size_t ret = num_bits;

  for (size_t i = 0; i < num_of_ints; ++i) {
    if (bits[i] == 0) {
      continue;
    }
    ret = (i * BITS_PER_INT) + __builtin_ctz(bits[i]);
    break;
  }

  return MIN(num_bits, ret);
}

// Hex dump function
//
// This function will dump the specified memory range with hex values and ASCII chars.
//
// - hex_dump(): the actual function printing the output.
//
//   Args:
//     prefix: a string outputting before each line.
//     ptr: the starting memory address to dump.
//     len: the length to dump (in bytes).
//
// - hex_dump_str(): for testing, returns one line only.
//
//   Args:
//     output: a output buffer larger than or equal to HEX_DUMP_BUF_SIZE bytes.
//     ptr: the starting memory address to dump.
//     len: the length to dump (in bytes).
//
//   Returns:
//     The output buffer given in the input argument. This is helpful in printf("%s").
//     NULL if the output buffer is too small.
//
static const char kNP = '.';  // the character used to show non-printable character.
void hex_dump(const char* prefix, const void* ptr, size_t len);
// for testing
#define HEX_DUMP_BUF_SIZE 70
char* hex_dump_str(char* output, size_t output_size, const void* ptr, size_t len);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_PORTING_H_
