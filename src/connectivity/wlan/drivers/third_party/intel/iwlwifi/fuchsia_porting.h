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

#include <stdlib.h>
#include <string.h>

#include <netinet/if_ether.h>
#include <zircon/compiler.h>

// TODO(WLAN-1038): Use Fuchsia-style integer types.
typedef unsigned long long u64;
typedef u64 __le64;
typedef unsigned long u32;
typedef u32 __le32;
typedef unsigned short u16;
typedef u16 __le16;
typedef unsigned char u8;
typedef u8 __u8;
typedef signed long s32;
typedef signed short s16;
typedef signed char s8;

typedef uint32_t netdev_features_t;

typedef uint64_t dma_addr_t;

typedef char* acpi_string;

// TODO(yjlou@): move to ieee80211.h
enum nl80211_iftype {
    NL80211_IFTYPE_UNSPECIFIED,
    NL80211_IFTYPE_ADHOC,
    NL80211_IFTYPE_STATION,
    NL80211_IFTYPE_AP,
    NL80211_IFTYPE_AP_VLAN,
    NL80211_IFTYPE_WDS,
    NL80211_IFTYPE_MONITOR,
    NL80211_IFTYPE_MESH_POINT,
    NL80211_IFTYPE_P2P_CLIENT,
    NL80211_IFTYPE_P2P_GO,
    NL80211_IFTYPE_P2P_DEVICE,
    NL80211_IFTYPE_OCB,
    NL80211_IFTYPE_NAN,

    /* keep last */
    NUM_NL80211_IFTYPES,
    NL80211_IFTYPE_MAX = NUM_NL80211_IFTYPES - 1
};

#define BIT(x) (1 << x)

#define DIV_ROUND_UP(num, div) (((num) + (div)-1) / (div))

#define le32_to_cpu(x) (x)
#define le32_to_cpup(x) (*x)
#define cpu_to_le32(x) (x)

#define BITS_PER_LONG (sizeof(long) * 8)

#define BITS_TO_LONGS(nr) (nr / BITS_PER_LONG)

#define ETHTOOL_FWVERS_LEN 32

#define IS_ENABLED(x) (false)

#define __printf(x, y)
#define __packed __PACKED
#define __bitwise
#define __force
#define __must_check __attribute__((warn_unused_result))
#define __aligned(x) __attribute__((aligned(x)))

// NEEDS_PORTING: Need to check if 'x' is static array.
#define ARRAY_SIZE(x) (countof(x))

// NEEDS_PORTING
#define WARN(x, y, z) \
    do {              \
    } while (0)
#define WARN_ON(x) (false)
#define WARN_ON_ONCE(x) (false)
#define BUILD_BUG_ON(x) (false)

// NEEDS_PORTING: Below structures are used in code but not ported yet.
struct device {};
struct dentry {};
struct napi_struct {};
struct sk_buff_head {};
struct mutex {};
struct firmware {
    size_t size;
    const u8* data;
    // NEEDS_PORTING struct page **pages;

    /* firmware loader private fields */
    void* priv;
};

static inline int test_bit(int nbits, const volatile unsigned long* addr) {
    return 1UL & (addr[nbits / BITS_PER_LONG] >> (nbits % BITS_PER_LONG));
}

static inline void set_bit(int nbits, unsigned long* addr) {
    addr[nbits / BITS_PER_LONG] |= 1UL << (nbits % BITS_PER_LONG);
}
#define __set_bit set_bit

static inline void clear_bit(int nbits, volatile unsigned long* addr) {
    addr[nbits / BITS_PER_LONG] &= ~(1UL << (nbits % BITS_PER_LONG));
}

static inline void might_sleep() {}

static inline void* vmalloc(unsigned long size) {
    return malloc(size);
}

static inline void* kmemdup(const void* src, size_t len) {
    void* dst = malloc(len);
    memcpy(dst, src, len);
    return dst;
}

static inline void vfree(const void* addr) {
    free((void*)addr);
}

static inline void kfree(void* addr) {
    free(addr);
}

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_PORTING_H_
