// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <garnet/lib/inet/ip_address.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/strings/string_printf.h>
#include <sys/socket.h>
#include <future>

#include "enclosed_guest.h"
#include "guest_test.h"

using ::testing::Each;
using ::testing::HasSubstr;

static constexpr char kVirtioNetUtil[] = "virtio_net_test_util";
static constexpr size_t kTestPacketSize = 100;

template <class T>
T* GuestTest<T>::enclosed_guest_ = nullptr;

class VirtioNetZirconGuest : public ZirconEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--virtio-net=true");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    // Disable netsvc to avoid spamming the net device with logs.
    launch_info->args.push_back("--cmdline-add=netsvc.disable=true");
    launch_info->args.push_back("--legacy-net=false");
    return ZX_OK;
  }

  void TestThread(uint8_t receive_byte, uint8_t send_byte) {
    async::Loop loop(&kAsyncLoopConfigAttachToThread);

    MockNetstack* netstack = GetNetstack();

    uint8_t pkt[kTestPacketSize];
    zx_status_t status =
        netstack->ReceivePacket(static_cast<void*>(pkt), kTestPacketSize);
    ASSERT_EQ(status, ZX_OK);
    EXPECT_THAT(pkt, Each(receive_byte));

    memset(pkt, send_byte, sizeof(pkt));
    status = netstack->SendPacket(static_cast<void*>(pkt), kTestPacketSize);
    ASSERT_EQ(status, ZX_OK);
  }
};

template <class T>
using VirtioNetTest = GuestTest<T>;

using GuestTypes = ::testing::Types<VirtioNetZirconGuest>;
TYPED_TEST_CASE(VirtioNetTest, GuestTypes);

TYPED_TEST(VirtioNetTest, ReceiveAndSend) {
  auto handle = std::async(std::launch::async, [this] {
    this->GetEnclosedGuest()->TestThread(0xab, 0xba);
  });

  std::string args =
      fxl::StringPrintf("%u %u %zu", 0xab, 0xba, kTestPacketSize);
  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioNetUtil, args, &result), ZX_OK);

  handle.wait();
  EXPECT_THAT(result, HasSubstr("PASS"));
}