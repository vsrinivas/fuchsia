// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fit/single_threaded_executor.h>
#include <sys/socket.h>

#include <future>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "enclosed_guest.h"
#include "guest_test.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/inet/ip_address.h"

using ::testing::Each;
using ::testing::HasSubstr;

static constexpr char kVirtioNetUtil[] = "virtio_net_test_util";
static constexpr size_t kTestPacketSize = 1000;

// Includes ethernet + IPv4 + UDP headers.
static constexpr size_t kHeadersSize = 42;

static constexpr fuchsia::hardware::ethernet::MacAddress kHostMacAddress = {
    .octets = {0x02, 0x1a, 0x11, 0x00, 0x01, 0x00},
};

class VirtioNetZirconGuest : public ZirconEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) override {
    launch_info->url = kZirconGuestUrl;
    launch_info->args = {
        "--virtio-gpu=false",
        "--virtio-net=true",
        "--cmdline-add=kernel.serial=none",
        // Disable netsvc to avoid spamming the net device with logs.
        "--cmdline-add=netsvc.disable=true",
    };
    return ZX_OK;
  }
};

class VirtioNetDebianGuest : public DebianEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) override {
    launch_info->url = kDebianGuestUrl;
    launch_info->args = {
        "--virtio-gpu=false",
        "--virtio-net=true",
    };
    return ZX_OK;
  }
};

static void TestThread(FakeNetstack* netstack, uint8_t receive_byte, uint8_t send_byte,
                       bool use_raw_packets) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // This thread will loop indefinitely until it receives the correct packet.
  // The test will time out via RunUtil in the test fixture if we fail to
  // receive the correct packet.
  while (true) {
    auto result = fit::run_single_threaded(netstack->ReceivePacket(kHostMacAddress));
    ASSERT_TRUE(result.is_ok());
    std::vector<uint8_t> packet = result.take_value();

    bool match_test_packet = false;
    size_t headers_size = use_raw_packets ? 0 : kHeadersSize;
    if (packet.size() == headers_size + kTestPacketSize) {
      match_test_packet = true;
      for (size_t i = headers_size; i != packet.size(); ++i) {
        if (packet[i] != receive_byte) {
          match_test_packet = false;
          break;
        }
      }
    }
    if (match_test_packet) {
      break;
    }
  }

  std::vector<uint8_t> send_packet(kTestPacketSize);
  memset(send_packet.data(), send_byte, kTestPacketSize);
  fit::promise<void, zx_status_t> promise;
  if (use_raw_packets) {
    promise = netstack->SendPacket(kHostMacAddress, std::move(send_packet));
  } else {
    promise = netstack->SendUdpPacket(kHostMacAddress, std::move(send_packet));
  }
  auto result = fit::run_single_threaded(std::move(promise));
  ASSERT_TRUE(result.is_ok());
}

using VirtioNetZirconGuestTest = GuestTest<VirtioNetZirconGuest>;

TEST_F(VirtioNetZirconGuestTest, ReceiveAndSend) {
  auto handle = std::async(std::launch::async, [this] {
    FakeNetstack* netstack = this->GetEnclosedGuest()->GetNetstack();
    TestThread(netstack, 0xab, 0xba, true /* use_raw_packets */);
  });

  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioNetUtil,
                          {
                              fxl::StringPrintf("%u", 0xab),
                              fxl::StringPrintf("%u", 0xba),
                              fxl::StringPrintf("%zu", kTestPacketSize),
                          },
                          &result),
            ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));

  handle.wait();
}

using VirtioNetDebianGuestTest = GuestTest<VirtioNetDebianGuest>;

TEST_F(VirtioNetDebianGuestTest, ReceiveAndSend) {
  auto handle = std::async(std::launch::async, [this] {
    FakeNetstack* netstack = this->GetEnclosedGuest()->GetNetstack();
    TestThread(netstack, 0xab, 0xba, false /* use_raw_packets */);
  });

  // Find the network interface corresponding to the guest's MAC address.
  std::string network_interface;
  ASSERT_EQ(this->RunUtil(kVirtioNetUtil,
                          {
                              "Find",
                              "02:1a:11:00:01:00",
                          },
                          &network_interface),
            ZX_OK);
  network_interface = fxl::TrimString(network_interface, "\n").ToString();
  ASSERT_FALSE(network_interface.empty());

  // Configure the guest IPv4 address.
  EXPECT_EQ(this->Execute({"ifconfig", network_interface, "192.168.0.10"}), ZX_OK);

  // Manually add a route to the host.
  EXPECT_EQ(this->Execute({"arp", "-s", "192.168.0.1", "02:1a:11:00:00:00"}), ZX_OK);

  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioNetUtil,
                          {
                              "Transfer",
                              fxl::StringPrintf("%u", 0xab),
                              fxl::StringPrintf("%u", 0xba),
                              fxl::StringPrintf("%zu", kTestPacketSize),
                          },
                          &result),
            ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));

  handle.wait();
}
