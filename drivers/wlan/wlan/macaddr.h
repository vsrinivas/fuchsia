// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstddef>
#include <string>

namespace wlan {

constexpr size_t kMacAddrLen = 6;  // bytes

struct MacAddr {
    uint8_t byte[kMacAddrLen];

    std::string ToString() const {
        char buf[17 + 1];
        std::snprintf(buf, 17 + 1, "%02x:%02x:%02x:%02x:%02x:%02x", byte[0], byte[1], byte[2],
                      byte[3], byte[4], byte[5]);
        return std::string(buf);
    }

    void Reset() { std::memset(byte, 0x00, kMacAddrLen); }

    int Cmp(const MacAddr& addr) const { return memcmp(byte, addr.byte, kMacAddrLen); }

    bool Equals(const MacAddr& addr) const { return Cmp(addr) == 0; }

    bool Gt(const MacAddr& addr) const { return Cmp(addr) > 0; }

    bool Lt(const MacAddr& addr) const { return Cmp(addr) < 0; }

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

    bool IsMcast() const { return byte[0] & 0x01; }

    bool IsLocalAdmin() const { return byte[0] & 0x02; }

    bool IsGroupAddr() const {
        // IEEE range: 01:80:c2:00:00:00 - 01:80:c2:ff:ff:ff
        return byte[0] == 0x01 && byte[1] == 0x80 && byte[2] == 0xc2;
    }

    // Overloaded initializer.
    void Set(const MacAddr& addr) { std::memcpy(byte, addr.byte, kMacAddrLen); }
    void Set(uint8_t addr[]) { std::memcpy(byte, addr, kMacAddrLen); }
    void Set(const std::string& addr) { FromStr(addr); }

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
} __PACKED;

// Defined in macaddr.cpp
extern const MacAddr kZeroMac;
extern const MacAddr kBcastMac;

}  // namespace wlan
