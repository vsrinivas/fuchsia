// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/guest_console.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/virtualization/tests/socket.h"

namespace {

class FakeSocket : public SocketInterface {
 public:
  explicit FakeSocket(std::vector<std::string> responses) : responses_(std::move(responses)) {}

  zx_status_t Send(zx::time deadline, const std::string& message) override { return ZX_OK; }

  zx_status_t Receive(zx::time deadline, std::string* result) override {
    if (reads_performed_ >= responses_.size()) {
      return ZX_ERR_PEER_CLOSED;
    }
    *result = responses_.at(reads_performed_);
    reads_performed_++;
    return ZX_OK;
  }

 private:
  size_t reads_performed_ = 0;
  std::vector<std::string> responses_;
};

TEST(GuestConsole, WaitForMarkerEmpty) {
  auto socket = std::make_unique<FakeSocket>(std::vector<std::string>{});
  GuestConsole console(std::move(socket));

  // Ensure an empty read returns success, and clears out result.
  std::string result = "non-empty";
  EXPECT_EQ(console.WaitForMarker("", &result), ZX_OK);
  EXPECT_EQ(result, "");

  // A non-empty marker read should return an error.
  EXPECT_EQ(console.WaitForMarker("x", &result), ZX_ERR_PEER_CLOSED);
}

TEST(GuestConsole, WaitForMarkerSimple) {
  auto socket = std::make_unique<FakeSocket>(std::vector<std::string>{"marker"});
  GuestConsole console(std::move(socket));

  std::string result;
  EXPECT_EQ(console.WaitForMarker("marker", &result), ZX_OK);
  EXPECT_EQ(result, "");
}

TEST(GuestConsole, WaitForMarkerContentBefore) {
  auto socket = std::make_unique<FakeSocket>(std::vector<std::string>{"xxxmarker"});
  GuestConsole console(std::move(socket));

  std::string result;
  EXPECT_EQ(console.WaitForMarker("marker", &result), ZX_OK);
  EXPECT_EQ(result, "xxx");
}

TEST(GuestConsole, WaitForMarkerContentAfterPreserved) {
  auto socket = std::make_unique<FakeSocket>(std::vector<std::string>{"xxxmarkeryyy", "second"});
  GuestConsole console(std::move(socket));

  std::string result;
  EXPECT_EQ(console.WaitForMarker("marker", &result), ZX_OK);
  EXPECT_EQ(result, "xxx");

  EXPECT_EQ(console.WaitForMarker("second", &result), ZX_OK);
  EXPECT_EQ(result, "yyy");
}

TEST(GuestConsole, WaitForMarkerSplitMarker) {
  // "xxx" + "marker" + "yyy" + "second"
  auto socket = std::make_unique<FakeSocket>(
      std::vector<std::string>{"xx", "xm", "ar", "keryyys", "econ", "d"});
  GuestConsole console(std::move(socket));

  std::string result;
  EXPECT_EQ(console.WaitForMarker("marker", &result), ZX_OK);
  EXPECT_EQ(result, "xxx");

  EXPECT_EQ(console.WaitForMarker("second", &result), ZX_OK);
  EXPECT_EQ(result, "yyy");
}

}  // namespace
