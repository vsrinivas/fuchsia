// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/macaddr.h>

#include <zircon/assert.h>

#include <unordered_map>

namespace wlan {
namespace common {

typedef uint64_t seq_hash_t;
typedef uint16_t seq_t;  // TODO(porce): Mesh uses [0, 2^32 - 1] space.

// IEEE Std 802.11-2016, 10.3.2.11.2, 10.3.2.11.3
template <seq_t modulo_divisor> class SequenceNumberSpace {
   public:
    seq_t SetTo(seq_t to) {
        if (to >= modulo_divisor) { to = to % modulo_divisor; }
        seq_ = to;
        return seq_;
    }

    seq_t GetLastUsed() { return seq_; }

    seq_t Next() { return SetTo(seq_ + 1); }

   private:
    seq_t seq_ = 0;
};

template <seq_t modulo_divisor> using Sns = SequenceNumberSpace<modulo_divisor>;

template <seq_t modulo_divisor>
using SnsMap = std::unordered_map<seq_hash_t, SequenceNumberSpace<modulo_divisor>>;

class Sequence {
   public:
    Sns<4096>* Sns1(const common::MacAddr& addr) {
        auto hash = Hasher(addr, 0);
        return Fetch<4096>(hash);
    }

    Sns<4096>* Sns2(const common::MacAddr& addr, uint8_t tid) {
        // IEEE Std 802.11-2016, 9.2.4.5.2
        // TID is 4 bit long.
        auto hash = Hasher(addr, tid);
        return Fetch<4096>(hash);
    }

    // Sns3 optional

    Sns<1024>* Sns4(const common::MacAddr& addr, uint8_t aci) {
        // IEEE Std 802.11-2016, 9.2.4.4.2
        // ACI subfield is 2 bit long.
        const uint8_t kTidBitLen = 4;
        auto hash = Hasher(addr, aci << kTidBitLen);  // Shift aci to avoid collision with TID
        return Fetch<1024>(hash);
    }

    Sns<4096>* Sns5() {
        // Arbitrary value by spec. Increment to assist debugging.
        static const seq_hash_t hash = 0x01 << (kMacAddrLen + 1);
        return Fetch<4096>(hash);
    }

   private:
    // A bucket is a value for either TID or ACI.
    seq_hash_t Hasher(const common::MacAddr& addr, uint8_t bucket = 0) const {
        // This hash takes advantage of MacAddr::ToU64()'s implementation
        // where most significant two bytes are unused.
        seq_hash_t hash = addr.ToU64();
        hash |= (bucket << kMacAddrLen);
        return hash;
    }

    template <seq_t modulo_divisor> Sns<modulo_divisor>* Fetch(seq_hash_t hash) {
        auto& map = GetSnsMap<modulo_divisor>();
        auto iter = map.find(hash);
        if (iter == map.end()) {
            auto pair = map.emplace(hash, Sns<modulo_divisor>());

            // Likely to be a serious memory shortage.
            ZX_DEBUG_ASSERT(pair.second);

            iter = pair.first;
        }
        return &iter->second;
    }

    template <seq_t modulo_divisor>
    typename std::enable_if<modulo_divisor == 1024, SnsMap<1024>&>::type GetSnsMap() {
        return sns_map1024_;
    }

    template <seq_t modulo_divisor>
    typename std::enable_if<modulo_divisor == 4096, SnsMap<4096>&>::type GetSnsMap() {
        return sns_map4096_;
    }

    SnsMap<1024> sns_map1024_;
    SnsMap<4096> sns_map4096_;
};

}  // namespace common
}  // namespace wlan
