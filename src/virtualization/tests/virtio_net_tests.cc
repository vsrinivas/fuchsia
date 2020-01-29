// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fit/single_threaded_executor.h>
#include <sys/socket.h>

#include <future>

#include <gtest/gtest.h>

#include "enclosed_guest.h"
#include "guest_test.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/inet/ip_address.h"
#include "src/virtualization/bin/vmm/guest_config.h"

using ::testing::Each;
using ::testing::HasSubstr;

static constexpr char kVirtioNetUtil[] = "virtio_net_test_util";
static constexpr size_t kTestPacketSize = 1000;

// Includes ethernet + IPv4 + UDP headers.
static constexpr size_t kHeadersSize = 42;

static constexpr fuchsia::hardware::ethernet::MacAddress kDefaultMacAddress = {
    .octets = {0x02, 0x1a, 0x11, 0x00, 0x01, 0x00},
};
static constexpr fuchsia::hardware::ethernet::MacAddress kSecondNicMacAddress = {
    .octets = {0x02, 0x1a, 0x11, 0x00, 0x01, 0x01},
};
static constexpr fuchsia::virtualization::NetSpec kSecondNicNetSpec = {
    .mac_address = kSecondNicMacAddress,
};
static constexpr char kDefaultMacString[] = "02:1a:11:00:01:00";
static constexpr char kSecondNicMacString[] = "02:1a:11:00:01:01";
static constexpr char kHostMacString[] = "02:1a:11:00:00:00";

class VirtioNetMultipleInterfacesZirconGuest : public ZirconEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) override {
    launch_info->url = kZirconGuestUrl;
    launch_info->guest_config.set_virtio_gpu(false);
    // Disable netsvc to avoid spamming the net device with logs.
    launch_info->guest_config.mutable_cmdline_add()->push_back(
        "kernel.serial=none netsvc.disable=true");
    launch_info->guest_config.mutable_net_devices()->emplace_back(kSecondNicNetSpec);
    return ZX_OK;
  }
};

class VirtioNetMultipleInterfacesDebianGuest : public DebianEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) override {
    launch_info->url = kDebianGuestUrl;
    launch_info->guest_config.set_virtio_gpu(false);
    launch_info->guest_config.mutable_net_devices()->emplace_back(kSecondNicNetSpec);
    return ZX_OK;
  }
};

static void TestThread(fuchsia::hardware::ethernet::MacAddress mac_addr, FakeNetstack* netstack,
                       uint8_t receive_byte, uint8_t send_byte, bool use_raw_packets) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // This thread will loop indefinitely until it receives the correct packet.
  // The test will time out via RunUtil in the test fixture if we fail to
  // receive the correct packet.
  while (true) {
    auto result = fit::run_single_threaded(netstack->ReceivePacket(mac_addr));
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
    promise = netstack->SendPacket(mac_addr, std::move(send_packet));
  } else {
    promise = netstack->SendUdpPacket(mac_addr, std::move(send_packet));
  }
  auto result = fit::run_single_threaded(std::move(promise));
  ASSERT_TRUE(result.is_ok());
}

using VirtioNetMultipleInterfacesDebianGuestTest =
    GuestTest<VirtioNetMultipleInterfacesDebianGuest>;

TEST_F(VirtioNetMultipleInterfacesDebianGuestTest, ReceiveAndSend) {
  auto handle = std::async(std::launch::async, [this] {
    FakeNetstack* netstack = this->GetEnclosedGuest()->GetNetstack();
    TestThread(kDefaultMacAddress, netstack, 0xab, 0xba, false /* use_raw_packets */);
  });

  // Find the network interface corresponding to the guest's first ethernet device MAC address.
  std::string network_interface;
  ASSERT_EQ(this->RunUtil(kVirtioNetUtil,
                          {
                              "Find",
                              kDefaultMacString,
                          },
                          &network_interface),
            ZX_OK);
  network_interface = fxl::TrimString(network_interface, "\n").ToString();
  ASSERT_FALSE(network_interface.empty());

  // Configure the guest IPv4 address.
  EXPECT_EQ(this->Execute({"ifconfig", network_interface, "192.168.0.10"}), ZX_OK);

  // Manually add a route to the host.
  EXPECT_EQ(this->Execute({"arp", "-s", "192.168.0.1", kHostMacString}), ZX_OK);

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

  // Bring down the first interface
  EXPECT_EQ(this->Execute({"ifconfig", network_interface, "down"}), ZX_OK);

  // Find the network interface corresponding to the guest's second ethernet device MAC address.
  ASSERT_EQ(this->RunUtil(kVirtioNetUtil,
                          {
                              "Find",
                              kSecondNicMacString,
                          },
                          &network_interface),
            ZX_OK);
  network_interface = fxl::TrimString(network_interface, "\n").ToString();
  ASSERT_FALSE(network_interface.empty());

  // Configure the guest's second interface with the same settings as the first interface.
  EXPECT_EQ(this->Execute({"ifconfig", network_interface, "192.168.0.10"}), ZX_OK);
  EXPECT_EQ(this->Execute({"arp", "-s", "192.168.0.1", kHostMacString}), ZX_OK);

  // Start a new handler thread to validate the data sent over the second NIC.
  handle = std::async(std::launch::async, [this] {
    FakeNetstack* netstack = this->GetEnclosedGuest()->GetNetstack();
    TestThread(kSecondNicMacAddress, netstack, 0xcd, 0xdc, false /* use_raw_packets */);
  });

  // Run the net util to generate and validate the data
  EXPECT_EQ(this->RunUtil(kVirtioNetUtil,
                          {
                              "Transfer",
                              fxl::StringPrintf("%u", 0xcd),
                              fxl::StringPrintf("%u", 0xdc),
                              fxl::StringPrintf("%zu", kTestPacketSize),
                          },
                          &result),
            ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));

  handle.wait();
}

using VirtioNetMultipleInterfacesZirconGuestTest =
    GuestTest<VirtioNetMultipleInterfacesZirconGuest>;

TEST_F(VirtioNetMultipleInterfacesZirconGuestTest, ReceiveAndSend) {
  // Loop back some data over the default network interface to verify that it is functional.
  auto handle = std::async(std::launch::async, [this] {
    FakeNetstack* netstack = this->GetEnclosedGuest()->GetNetstack();
    TestThread(kDefaultMacAddress, netstack, 0xab, 0xba, true /* use_raw_packets */);
  });

  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioNetUtil,
                          {fxl::StringPrintf("%u", 0xab), fxl::StringPrintf("%u", 0xba),
                           fxl::StringPrintf("%zu", kTestPacketSize), kDefaultMacString},
                          &result),
            ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));

  handle.wait();

  // Ensure that the guest's second NIC works as well.
  handle = std::async(std::launch::async, [this] {
    FakeNetstack* netstack = this->GetEnclosedGuest()->GetNetstack();
    TestThread(kSecondNicMacAddress, netstack, 0xcd, 0xdc, true /* use_raw_packets */);
  });

  EXPECT_EQ(this->RunUtil(kVirtioNetUtil,
                          {fxl::StringPrintf("%u", 0xcd), fxl::StringPrintf("%u", 0xdc),
                           fxl::StringPrintf("%zu", kTestPacketSize), kSecondNicMacString},
                          &result),
            ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));

  handle.wait();
}
