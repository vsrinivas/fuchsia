// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fastboot.h"

#include <functional>
#include <queue>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

constexpr uint16_t kTestUdpPort = 1234;
constexpr ip6_addr kTestIpAddress = {0x01, 0x23, 0x45, 0x67};

// Defined by fastboot documentation.
enum UdpPacketType {
  kUdpPacketTypeError = 0x00,
  kUdpPacketTypeQuery = 0x01,
  kUdpPacketTypeInit = 0x02,
  kUdpPacketTypeFastboot = 0x03
};

// Extracts a network-order uint16 from the given data.
uint16_t ExtractU16(const uint8_t* data) { return static_cast<uint16_t>(data[0] << 8) | data[1]; }

// Extracts a uint16 MSB and LSB to reduce boilerplate.
uint8_t U16Msb(uint16_t value) { return value >> 8; }
uint8_t U16Lsb(uint16_t value) { return value & 0xFF; }

// Returns true if |data| starts with |prefix|.
bool StartsWith(const std::vector<uint8_t>& data, const std::vector<uint8_t>& prefix) {
  return data.size() >= prefix.size() && memcmp(data.data(), prefix.data(), prefix.size()) == 0;
}

// Helper to fake fastboot UDP input and output.
//
// On creation, injects custom UDP functions into fastboot so we can run the
// loop while controlling what gets sent. On deletion, returns fastboot to
// normal functionality.
//
// Only one of these object can live at a time.
class FakeFastbootUdp {
 public:
  FakeFastbootUdp() {
    if (instance_) {
      fprintf(stderr, "ERROR: cannot create multiple FakeFastbootUdp objects - exiting\n");
      exit(1);
    }
    instance_ = this;
    fb_set_udp_functions_for_testing(RawPollFunc, RawSendFunc);
  }

  ~FakeFastbootUdp() {
    instance_ = nullptr;
    fb_set_udp_functions_for_testing(nullptr, nullptr);
  }

  // Queues a packet to be sent on the next fastboot loop.
  void QueuePacket(std::vector<uint8_t> data) { tx_packets_.push(std::move(data)); }

  // Pops the earliest received packet, or an empty packet if none exist.
  std::vector<uint8_t> ReceivePacket() {
    if (rx_packets_.empty()) {
      return {};
    }
    auto ret = std::move(rx_packets_.front());
    rx_packets_.pop();
    return ret;
  }

  // Sends and receives the 4 packets necessary to initialize a fastboot UDP
  // session: TX query -> RX query response -> TX init -> RX init response.
  //
  // Registers test failure on error.
  void InitializeSession() {
    fb_bootimg_t bootimg;

    // Send a query packet to get the current sequence number.
    const auto query_packet = std::vector<uint8_t>{kUdpPacketTypeQuery, 0, 0, 0};
    QueuePacket(query_packet);
    ASSERT_EQ(POLL, fb_poll(&bootimg));

    // Response packet should be the same query with 2 additional bytes giving
    // the current sequence number.
    auto response = ReceivePacket();
    ASSERT_EQ(response.size(), 6u);
    ASSERT_TRUE(StartsWith(response, query_packet));
    uint16_t sequence = ExtractU16(&response[4]);

    // Send an init packet: version = 0x0001, max packet size = 0x0800.
    QueuePacket(
        {kUdpPacketTypeInit, 0x00, U16Msb(sequence), U16Lsb(sequence), 0x00, 0x01, 0x08, 0x00});
    ASSERT_EQ(POLL, fb_poll(&bootimg));

    response = ReceivePacket();
    ASSERT_EQ(response.size(), 8u);
    ASSERT_TRUE(
        StartsWith(response, {kUdpPacketTypeInit, 0x00, U16Msb(sequence), U16Lsb(sequence)}));
    // UDP protocol version should be 1.
    ASSERT_EQ(ExtractU16(&response[4]), 1);
    // Should support at least 512-byte packets or downloads will take forever.
    ASSERT_GE(ExtractU16(&response[6]), 512);
  }

 private:
  // Raw C functions to bounce into our active instance.
  static void RawPollFunc() { instance_->PollFunc(); }
  static int RawSendFunc(const void* data, size_t len, const ip6_addr* daddr, uint16_t dport,
                         uint16_t sport) {
    return instance_->SendFunc(data, len, daddr, dport, sport);
  }

  // Sends the next packet.
  void PollFunc() {
    if (!tx_packets_.empty()) {
      std::vector<uint8_t>& packet = tx_packets_.front();
      fb_recv(packet.data(), packet.size(), &kTestIpAddress, kTestUdpPort);
      tx_packets_.pop();
    }
  }

  int SendFunc(const void* data, size_t len, const ip6_addr* daddr, uint16_t dport,
               uint16_t sport) {
    EXPECT_EQ(memcmp(daddr, &kTestIpAddress, sizeof(kTestIpAddress)), 0);
    EXPECT_EQ(dport, kTestUdpPort);
    EXPECT_EQ(sport, FB_SERVER_PORT);

    const uint8_t* data_bytes = reinterpret_cast<const uint8_t*>(data);
    rx_packets_.emplace(data_bytes, data_bytes + len);
    return 0;
  }

  // Global instance to bounce raw C functions into our object.
  static FakeFastbootUdp* instance_;

  std::queue<std::vector<uint8_t>> tx_packets_;
  std::queue<std::vector<uint8_t>> rx_packets_;
};

FakeFastbootUdp* FakeFastbootUdp::instance_ = nullptr;

TEST(Fastboot, InitializeUdpSession) {
  FakeFastbootUdp udp;
  ASSERT_NO_FATAL_FAILURE(udp.InitializeSession());
}

}  // namespace
