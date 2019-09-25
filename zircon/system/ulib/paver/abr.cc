// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "abr.h"

#include <endian.h>
#include <lib/cksum.h>
#include <stdio.h>
#include <string.h>

namespace abr {

bool Client::IsValid() const {
  return memcmp(Data().magic, kMagic, kMagicLen) == 0 && Data().version_major == kMajorVersion &&
         Data().version_minor == kMinorVersion && Data().slots[0].priority <= kMaxPriority &&
         Data().slots[1].priority <= kMaxPriority &&
         Data().slots[0].tries_remaining <= kMaxTriesRemaining &&
         Data().slots[1].tries_remaining <= kMaxTriesRemaining &&
         Data().crc32 == htobe32(crc32(0, reinterpret_cast<const uint8_t*>(&Data()),
                                       sizeof(abr::Data) - sizeof(uint32_t)));
}

void Client::UpdateCrc(abr::Data* data) {
  data->crc32 = htobe32(
      crc32(0, reinterpret_cast<const uint8_t*>(data), sizeof(abr::Data) - sizeof(uint32_t)));
}

}  // namespace abr
