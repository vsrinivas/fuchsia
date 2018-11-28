// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_utils.h"
#include <fuchsia/overnet/cpp/fidl.h>
#include <gtest/gtest.h>

namespace overnetstack {
namespace fidl_utils_test {

TEST(FidlUtils, EncodeDecode) {
  fuchsia::overnet::Peer peer;
  peer.id = 123;
  peer.description.services.push_back("fuchsia.overnet.Overnet");
  auto round_tripped_peer =
      DecodeMessage<fuchsia::overnet::Peer>(EncodeMessage(&peer));
  EXPECT_TRUE(round_tripped_peer.is_ok()) << round_tripped_peer.AsStatus();
  EXPECT_EQ(round_tripped_peer->id, 123u);
}

}  // namespace fidl_utils_test
}  // namespace overnetstack
