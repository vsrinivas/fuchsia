// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/system_monitor/dockyard_host/dockyard_host.h"

#include "gtest/gtest.h"

class SystemMonitorDockyardHostTest : public ::testing::Test {
 public:
  void SetUp() {}

  dockyard::OnConnectionCallback GetConnectionHandler() const {
    return dockyard_host.dockyard_.on_connection_handler_;
  }

  dockyard::OnPathsCallback GetPathsHandler() const {
    return dockyard_host.dockyard_.on_paths_handler_;
  }

  dockyard::OnStreamSetsCallback GetStreamSetsHandler() const {
    return dockyard_host.dockyard_.on_stream_sets_handler_;
  }

  DockyardHost dockyard_host;
};

namespace {

TEST_F(SystemMonitorDockyardHostTest, ProcessRequests) {
  EXPECT_TRUE(GetConnectionHandler());
  EXPECT_TRUE(GetPathsHandler());
  EXPECT_TRUE(GetStreamSetsHandler());
  dockyard_host.StartCollectingFrom("apple.banana.carrot.dog");
  dockyard_host.Dockyard().ProcessRequests();
  // TODO(57): Add checks to see that requests were processed.
  // TODO(56): Add test for getting total system memory.
}

}  // namespace
