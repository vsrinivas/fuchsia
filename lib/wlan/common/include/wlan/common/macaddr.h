// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstddef>
#include <string>

namespace wlan {
namespace common {

// This macro provides memory safe C-style string.
#define MACSTR(mac_addr) (mac_addr).ToString().c_str()

// ISO/IEC 10039
// See http://standards.ieee.org/develop/regauth/tut/macgrp.pdf
constexpr size_t kMacAddrLen = 6;  // bytes

struct MacAddr {
    // Consider if this should be private. Pay attention to byte alignment.
    uint8_t byte[kMacAddrLen];

    MacAddr() {}
    MacAddr(const MacAddr& addr) { Set(addr); }
    explicit MacAddr(const std::string& addr) { Set(addr); }
    explicit MacAddr(const uint8_t addr[kMacAddrLen]) { Set(addr); }
    explicit MacAddr(std::initializer_list<uint8_t> addr) { Set(addr); }
    explicit MacAddr(uint64_t val) {
        for (size_t i = 0; i < kMacAddrLen; i++) {
            byte[kMacAddrLen - 1 - i] = val & 0xff;
            val >>= 8;
        }
    }

    std::string ToString() const {
        char buf[17 + 1];
        std::snprintf(buf, 17 + 1, "%02x:%02x:%02x:%02x:%02x:%02x", byte[0], byte[1], byte[2],
                      byte[3], byte[4], byte[5]);
        return std::string(buf);
    }

    void Reset() { std::memset(byte, 0x00, kMacAddrLen); }

    int Cmp(const MacAddr& addr) const { return memcmp(byte, addr.byte, kMacAddrLen); }

    // TODO(porce): inline
    bool operator==(const MacAddr& addr) const { return Cmp(addr) == 0; }
    bool operator!=(const MacAddr& addr) const { return !(*this == addr); }
    bool operator>(const MacAddr& addr) const { return Cmp(addr) > 0; }
    bool operator<(const MacAddr& addr) const { return Cmp(addr) < 0; }

    bool IsZero() const {
        // Note kZeroMac not used here. struct forward declaration can't be used.
        for (size_t idx = 0; idx < kMacAddrLen; idx++) {
            if (byte[idx] != 0x00) return false;
        }
        return true;
    }

    bool IsBcast() const {
        // Note kBcastMac not used here. struct forward declaration can't be used.
        for (size_t idx = 0; idx < kMacAddrLen; idx++) {
            if (byte[idx] != 0xff) return false;
        }
        return true;
    }

    bool IsUcast() const { return (byte[0] & 0x01) == 0; }

    bool IsMcast() const { return !IsUcast(); }

    bool IsLocalAdmin() const { return byte[0] & 0x02; }

    bool IsStdGroupAddr() const {
        // IEEE 802.1D MAC bridge Standard MAC Group Addresses
        // IEEE range: 01:80:c2:00:00:00 - 01:80:c2:ff:ff:ff
        if (byte[0] == 0x01 && byte[1] == 0x80 && byte[2] == 0xc2) { return true; }

        // ISO 9542 MAC Group Addresses
        if (byte[0] == 0x09 && byte[1] == 0x00 && byte[2] == 0x2b && byte[3] == 0x00 &&
            byte[4] == 0x00 && (byte[5] == 0x04 || byte[5] == 0x05)) {
            return true;
        }

        // TokenRing functional addresses not honored.

        return false;
    }

    // IEEE 802.11-2016, 9.2.4.3.3
    bool IsGroupAddr() const { return IsMcast() || IsBcast(); }

    // Overloaded initializers.
    void Set(const MacAddr& addr) { std::memcpy(byte, addr.byte, kMacAddrLen); }
    void Set(const std::string& addr) { FromStr(addr); }
    void Set(const uint8_t addr[kMacAddrLen]) { std::memcpy(byte, addr, kMacAddrLen); }
    void Set(std::initializer_list<uint8_t> addr) {
        if (addr.size() == kMacAddrLen) std::copy(addr.begin(), addr.end(), byte);
    }

    bool FromStr(const std::string& str) {
        // Accepted format:   xx:xx:xx:xx:xx:xx
        // TODO(porce): Support xx-xx-xx-xx-xx-xx, xxxx.xxxx.xxxx, xxxxxxxxxxxx
        if (str.length() != 17) { return false; }

        unsigned int tmp[kMacAddrLen];
        int result = std::sscanf(str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &tmp[0], &tmp[1],
                                 &tmp[2], &tmp[3], &tmp[4], &tmp[5]);
        if (kMacAddrLen != static_cast<size_t>(result)) { return false; }
        for (size_t idx = 0; idx < kMacAddrLen; idx++) {
            byte[idx] = (uint8_t)tmp[idx];
        }
        return true;
    }

    uint64_t ToU64() const {
        uint64_t m = 0;
        for (size_t idx = 0; idx < kMacAddrLen; idx++) {
            m <<= 8;
            m |= byte[idx];
        }
        return m;
    }

    inline void* CopyTo(void* dst) const { return memcpy(dst, byte, kMacAddrLen); }
} __PACKED;

struct MacAddrHasher {
    std::size_t operator()(const MacAddr& addr) const {
        return std::hash<uint64_t>()(addr.ToU64());
    }
};

// Defined in macaddr.cpp
extern const MacAddr kZeroMac;
extern const MacAddr kBcastMac;

}  // namespace common
}  // namespace wlan
