// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_COMPILER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_COMPILER_H_

// This file contains Fuchsia-specific compiler support code.

#include <endian.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zircon/compiler.h>

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

typedef uint32_t __be32;
typedef uint16_t __be16;
typedef uint64_t __le64;
typedef uint32_t __le32;
typedef uint16_t __le16;
typedef uint8_t __s8;
typedef uint8_t __u8;

#define BIT(x) (1UL << (x))

#define DIV_ROUND_UP(num, div) (((num) + (div)-1) / (div))

// Endianness byteswap macros.
#define le16_to_cpu(x) le16toh(x)
#define le32_to_cpu(x) le32toh(x)
#define le64_to_cpu(x) le64toh(x)
#define cpu_to_le16(x) htole16(x)
#define cpu_to_le32(x) htole32(x)
#define cpu_to_le64(x) htole64(x)

// Endianness access of possibly unaligned data.
static inline uint16_t le16_to_cpup(const uint16_t* x) {
  uint16_t val = 0;
  memcpy(&val, x, sizeof(val));
  return le16toh(val);
}

static inline uint32_t le32_to_cpup(const uint32_t* x) {
  uint32_t val = 0;
  memcpy(&val, x, sizeof(val));
  return le32toh(val);
}

static inline uint16_t be16_to_cpup(const uint16_t* x) {
  uint16_t val = 0;
  memcpy(&val, x, sizeof(val));
  return be16toh(val);
}

#define lower_32_bits(x) (x & 0xffffffff)
#define upper_32_bits(x) (x >> 32)

#define BITS_PER_BYTE 8
#define BITS_PER_INT (sizeof(int) * BITS_PER_BYTE)
#define BITS_PER_LONG (sizeof(long) * BITS_PER_BYTE)

#define BITS_TO_INTS(nr) (DIV_ROUND_UP(nr, BITS_PER_INT))
#define BITS_TO_LONGS(nr) (DIV_ROUND_UP(nr, BITS_PER_LONG))

#define __aligned(x) __attribute__((aligned(x)))
#define __force
#define __must_check __attribute__((warn_unused_result))
#define __packed __PACKED
#define __rcu                         // NEEDS_TYPES
#define ____cacheline_aligned_in_smp  // NEEDS_TYPES

// NEEDS_TYPES: Need to check if 'x' is static array.
#define ARRAY_SIZE(x) (countof(x))

#define offsetofend(type, member) (offsetof(type, member) + sizeof(((type*)NULL)->member))

// NEEDS_TYPES: need to be generic
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

typedef struct {
  int value;
} atomic_t;

////
// Inline functions
////
static inline int test_bit(long nbits, const volatile unsigned long* addr) {
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

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define max_t(type, a, b) MAX((type)(a), (type)(b))
#define min_t(type, a, b) MIN((type)(a), (type)(b))

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

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_COMPILER_H_
