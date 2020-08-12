// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/bin/dockyard_host/dockyard_host.h"

#include <gtest/gtest.h>

class SystemMonitorDockyardHostTest : public ::testing::Test {
 public:
  void SetUp() override {}

  dockyard::OnConnectionCallback GetConnectionHandler() const {
    return dockyard_host.dockyard_.on_connection_handler_;
  }

  dockyard::OnPathsCallback GetPathsHandler() const {
    return dockyard_host.dockyard_.on_paths_handler_;
  }

  DockyardHost dockyard_host;
};

namespace {

TEST_F(SystemMonitorDockyardHostTest, ProcessRequests) {
  EXPECT_TRUE(GetPathsHandler());
  dockyard_host.StartCollectingFrom("apple.banana.carrot.dog");
  dockyard_host.Dockyard().ProcessRequests();
  // TODO(fxbug.dev/57): Add checks to see that requests were processed.
  // TODO(fxbug.dev/56): Add test for getting total system memory.
}

}  // namespace
