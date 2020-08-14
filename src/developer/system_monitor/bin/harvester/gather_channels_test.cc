// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_channels.h"

#include <gtest/gtest.h>

#include "dockyard_proxy_fake.h"
#include "root_resource.h"

namespace {

// Get a dockyard path for the given |koid| and |suffix| key.
std::string KoidPath(uint64_t koid, const std::string& suffix) {
  std::ostringstream out;
  out << "koid:" << koid << ":" << suffix;
  return out.str();
}

}  // namespace.

class GatherChannelsTest : public ::testing::Test {
 public:
  void SetUp() override {}
};

TEST_F(GatherChannelsTest, SmokeTest) {
  zx_handle_t root_resource;
  ASSERT_EQ(harvester::GetRootResource(&root_resource), ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;
  harvester::GatherChannels gatherer(root_resource, &dockyard_proxy);
  harvester::g_slow_data_task_tree.Gather();
  gatherer.Gather();
  // Verify that something is being sent.
  EXPECT_TRUE(dockyard_proxy.CheckValueSubstringSent("type"));
  EXPECT_TRUE(dockyard_proxy.CheckValueSubstringSent("process"));
  EXPECT_TRUE(dockyard_proxy.CheckValueSubstringSent("peer"));

  // TODO(fxbug.dev/54364): add channel information when dockyard supports multi
  // maps.
  // EXPECT_TRUE(dockyard_proxy.CheckValueSubstringSent("channel"));
}

TEST_F(GatherChannelsTest, ProcessesAndPeers) {
  zx_handle_t root_resource;
  ASSERT_EQ(harvester::GetRootResource(&root_resource), ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;
  harvester::GatherChannels gatherer(root_resource, &dockyard_proxy);
  harvester::g_slow_data_task_tree.Gather();
  gatherer.Gather();

  // Check a peer.
  std::string near_peer_path;
  uint64_t near_channel_peer;
  EXPECT_TRUE(dockyard_proxy.CheckValueSubstringSent(":peer", &near_peer_path,
                                                     &near_channel_peer));
  // Determine the channel from the path.
  size_t begin = near_peer_path.find(':');
  ASSERT_NE(begin, std::string::npos);
  ++begin;
  size_t end = near_peer_path.find(':', begin);
  ASSERT_NE(end, std::string::npos);
  std::string near_channel_str = near_peer_path.substr(begin, end - begin);
  uint64_t near_channel = std::stoull(near_channel_str);

  uint64_t type;
  EXPECT_TRUE(
      dockyard_proxy.CheckValueSent(::KoidPath(near_channel, "type"), &type));
  EXPECT_EQ(type, dockyard::KoidType::CHANNEL);

  uint64_t far_channel_peer;
  if (dockyard_proxy.CheckValueSent(::KoidPath(near_channel_peer, "peer"),
                                    &far_channel_peer)) {
    EXPECT_EQ(near_channel, far_channel_peer);
    EXPECT_TRUE(dockyard_proxy.CheckValueSent(
        ::KoidPath(near_channel_peer, "type"), &type));
    EXPECT_EQ(type, dockyard::KoidType::CHANNEL);
  }
}
