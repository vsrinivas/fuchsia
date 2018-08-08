// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <wlan/common/logging.h>
#include <wlan/common/macaddr.h>
#include <zircon/types.h>

#include <unordered_map>

namespace wlan {

namespace macaddr_map_type {
enum MapType : uint8_t {
    kBss = 1,
};
}  // namespace macaddr_map_type

template <typename V, macaddr_map_type::MapType Type> class MacAddrMap {
   public:
    using size_type = typename std::unordered_map<uint64_t, V>::size_type;

    MacAddrMap(size_type limit = kNoLimit) : limit_(limit) {}
    ~MacAddrMap() { Clear(); }

    V Lookup(const common::MacAddr& addr) const {
        auto iter = map_.find(addr.ToU64());
        if (iter == map_.end()) { return V{}; }
        return (iter->second);
    }

    void Clear() { map_.clear(); }

    bool Remove(const common::MacAddr& addr) {
        auto iter = map_.find(addr.ToU64());
        if (iter == map_.end()) { return false; }
        iter->second.reset();
        map_.erase(iter);
        return true;
    }

    // Predicate signature is bool(V v).
    template <typename Predicate> size_type RemoveIf(Predicate p) {
        size_type removed = 0;
        for (auto iter = begin(map_); iter != end(map_);) {
            if (p(iter->second)) {
                iter->second.reset();
                iter = map_.erase(iter);
                removed++;
            } else {
                iter++;
            }
        }
        return removed;
    }

    // Function signature is void(V v).
    template <typename Function> void ForEach(Function f) {
        for (auto iter = begin(map_); iter != end(map_); iter++) {
            f(iter->second);
        }
    }

    bool IsFull() const { return limit_ != kNoLimit && Count() >= limit_; }

    zx_status_t Insert(const common::MacAddr& addr, V v) {
        if (IsFull()) {
            debugf("[MacAddrMap-%u] Already full\n", Type);
            return ZX_ERR_BAD_STATE;
        }

        if (Lookup(addr)) {
            debugf("[MacAddrMap-%u] Duplicate insert declined for Address: %s\n", Type,
                   MACSTR(addr));
            return ZX_ERR_ALREADY_EXISTS;
        }

        map_.emplace(addr.ToU64(), v);
        return ZX_OK;
    }

    inline size_type Count() const { return map_.size(); }

    static constexpr size_type kNoLimit = 0;

   private:
    // TODO(porce): Consider using MacAddr as key.
    std::unordered_map<uint64_t, V> map_;
    size_type limit_;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MacAddrMap);
};

}  // namespace wlan
