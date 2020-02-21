// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file provides callout implementations of 16-byte atomic operations. When the compiler fails
// to provide inline intrinsics for a given atomic operation, it will generate a call to one of
// these functions. In particular this is needed by GCC to support 16-byte operations.
//
// These implementations provide __ATOMIC_SEQ_CST semantics regardless of the requested "memory
// model" argument.  As a result, they are not necessarily optimal (esp. on arm64), but they will be
// correct because __ATOMIC_SEQ_CST has the strongest semantics.

#include <cstdint>

bool atomic_compare_exchange_16(volatile void* ptr, void* expected, unsigned __int128 desired, bool,
                                int, int) __asm__("__atomic_compare_exchange_16");

unsigned __int128 atomic_load_16(volatile void* ptr, int) __asm__("__atomic_load_16");

void atomic_store_16(volatile void* ptr, unsigned __int128 value, int) __asm__("__atomic_store_16");

#ifdef __aarch64__

bool atomic_compare_exchange_16(volatile void* ptr_void, void* expected_void,
                                unsigned __int128 desired, bool, int, int) {
  auto ptr = static_cast<volatile unsigned __int128*>(ptr_void);
  auto expected = static_cast<unsigned __int128*>(expected_void);
  int result;
  do {
    uint64_t temp_lo;
    uint64_t temp_hi;
    __asm__ volatile("ldaxp %[lo], %[hi], %[src]"
                     : [ lo ] "=r"(temp_lo), [ hi ] "=r"(temp_hi)
                     : [ src ] "Q"(*ptr)
                     : "memory");
    unsigned __int128 temp = (static_cast<unsigned __int128>(temp_hi)) << 64 | temp_lo;

    if (temp != *expected) {
      // No reason to leave the monitor in Exclusive so clear it.
      __asm__ volatile("clrex");
      *expected = temp;
      return false;
    }

    auto desired_lo = static_cast<uint64_t>(desired);
    auto desired_hi = static_cast<uint64_t>(desired >> 64);
    __asm__ volatile("stlxp %w[result], %[lo], %[hi], %[ptr]"
                     : [ result ] "=&r"(result), [ ptr ] "=Q"(*ptr)
                     : [ lo ] "r"(desired_lo), [ hi ] "r"(desired_hi)
                     : "memory");
  } while (result);
  return true;
}

unsigned __int128 atomic_load_16(volatile void* ptr_void, int) {
  auto ptr = static_cast<volatile unsigned __int128*>(ptr_void);
  uint64_t value_lo;
  uint64_t value_hi;
  int result;
  do {
    __asm__ volatile("ldaxp %[lo], %[hi], %[ptr]"
                     : [ lo ] "=r"(value_lo), [ hi ] "=r"(value_hi)
                     : [ ptr ] "Q"(*ptr));
    __asm__ volatile("stlxp %w[result], %[lo], %[hi], %[ptr]"
                     : [ result ] "=&r"(result), [ ptr ] "=Q"(*ptr)
                     : [ lo ] "r"(value_lo), [ hi ] "r"(value_hi)
                     : "memory");
  } while (result);
  return (static_cast<unsigned __int128>(value_hi)) << 64 | value_lo;
}

void atomic_store_16(volatile void* ptr_void, unsigned __int128 value, int) {
  auto ptr = static_cast<volatile unsigned __int128*>(ptr_void);
  auto value_lo = static_cast<uint64_t>(value);
  auto value_hi = static_cast<uint64_t>(value >> 64);
  uint64_t temp_lo;
  uint64_t temp_hi;
  int result;
  do {
    __asm__ volatile("ldaxp %[temp_lo], %[temp_hi], %[ptr]"
                     : [ temp_lo ] "=r"(temp_lo), [ temp_hi ] "=r"(temp_hi)
                     : [ ptr ] "Q"(*ptr));
    __asm__ volatile("stlxp %w[result], %[value_lo], %[value_hi], %[ptr]"
                     : [ result ] "=&r"(result), [ ptr ] "=Q"(*ptr)
                     : [ value_lo ] "r"(value_lo), [ value_hi ] "r"(value_hi)
                     : "memory");
  } while (result);
}

#elif defined(__x86_64__)

bool atomic_compare_exchange_16(volatile void* ptr_void, void* expected_void,
                                unsigned __int128 desired, bool, int, int) {
  auto ptr = static_cast<volatile unsigned __int128*>(ptr_void);
  auto expected = static_cast<unsigned __int128*>(expected_void);
  auto desired_lo = static_cast<uint64_t>(desired);
  auto desired_hi = static_cast<uint64_t>(desired >> 64);
  bool result;
  __asm__ volatile("lock cmpxchg16b %[dest]"
                   : [ dest ] "+m"(*ptr), [ result ] "=@ccz"(result), "+A"(*expected)
                   : "b"(desired_lo), "c"(desired_hi)
                   : "memory");
  return result;
}

unsigned __int128 atomic_load_16(volatile void* ptr_void, int) {
  auto ptr = static_cast<volatile unsigned __int128*>(ptr_void);
  unsigned __int128 result = 0;
  __asm__ volatile("lock cmpxchg16b %[dest]"
                   : [ dest ] "+m"(*ptr), "+A"(result)
                   : "b"(0), "c"(0)
                   : "cc", "memory");
  return result;
}

void atomic_store_16(volatile void* ptr_void, unsigned __int128 value, int) {
  auto ptr = static_cast<volatile unsigned __int128*>(ptr_void);
  auto value_lo = static_cast<uint64_t>(value);
  auto value_hi = static_cast<uint64_t>(value >> 64);
  unsigned __int128 expected = 0;
  bool matched;
  do {
    __asm__ volatile("lock cmpxchg16b %[dest]"
                     : [ dest ] "+m"(*ptr), [ result ] "=@ccz"(matched), "+A"(expected)
                     : "b"(value_lo), "c"(value_hi)
                     : "memory");
  } while (!matched);
}

#endif
