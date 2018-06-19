// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_PACKET_INFO_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_PACKET_INFO_H_

#include <cstddef>
#include <cstdint>

namespace media_player {
namespace test {

class PacketInfo {
 public:
  static uint64_t Hash(const void* data, size_t data_size);

  PacketInfo(int64_t timestamp, uint64_t size, uint64_t hash)
      : timestamp_(timestamp), size_(size), hash_(hash) {}

  int64_t timestamp() const { return timestamp_; }
  uint64_t size() const { return size_; }
  uint64_t hash() const { return hash_; }

 private:
  int64_t timestamp_;
  uint64_t size_;
  uint64_t hash_;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_PACKET_INFO_H_
