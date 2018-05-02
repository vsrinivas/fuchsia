// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/mac_frame.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <zircon/types.h>

namespace wlan {

// Bitmap tracking buffered traffic for BSS clients.
// Can derive and write a 'Partial Virtual Bitmap' for TIM Element usage.
// See IEEE 802.11-2016, 9.4.2.6 for more information.
class TrafficIndicationMap {
   public:
    TrafficIndicationMap();
    void SetTrafficIndication(aid_t aid, bool has_bu);

    // Write a Partial Virtual Bitmap into the given buffer.
    zx_status_t WritePartialVirtualBitmap(uint8_t* buf, size_t buf_len, size_t* bitmap_len,
                                          uint8_t* bitmap_offset) const;
    bool HasDozingClients() const;
    bool HasGroupTraffic() const;
    void Clear();

   private:
    // N1 and N2 specify the start and end offsets of a range of AIDs which have
    // buffered traffic. N1 is the largest even number such that AIDs from 1 to
    // (N1 * 8) - 1 have no buffered traffic. N2 is the smallest number such that
    // AIDs from (N2 + 1) * 8 to 2007 have no buffered traffic. These offsets are
    // used to compute a Partial Virtual Bitmap. See IEEE 802.11-2016, 9.4.2.6.
    size_t N1() const;
    size_t N2() const;

    bitmap::RawBitmapGeneric<bitmap::FixedStorage<kMaxBssClients>> aid_bitmap_;
};

}  // namespace wlan