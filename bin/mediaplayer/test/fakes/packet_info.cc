// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/test/fakes/packet_info.h"

namespace media_player {
namespace test {

// static
uint64_t PacketInfo::Hash(const void* data, size_t data_size) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  uint64_t hash = 0;

  for (; data_size != 0; --data_size, ++bytes) {
    hash = *bytes + (hash << 6) + (hash << 16) - hash;
  }

  return hash;
}

}  // namespace test
}  // namespace media_player
