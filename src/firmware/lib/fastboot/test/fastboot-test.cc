// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fastboot/fastboot.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace fastboot {
namespace {

using Packets = std::vector<std::string>;

class TestTransport : public Transport {
 public:
  void AddInPacket(const void* data, size_t size) {
    const char* start = static_cast<const char*>(data);
    in_packets_.insert(in_packets_.begin(), std::string(start, start + size));
  }

  const Packets& GetOutPackets() { return out_packets_; }

  zx::status<size_t> ReceivePacket(void* dst, size_t capacity) override {
    if (in_packets_.empty()) {
      return zx::error(ZX_ERR_BAD_STATE);
    }

    const std::string& packet = in_packets_.back();
    if (packet.size() > capacity) {
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    }

    size_t size = packet.size();
    memcpy(dst, packet.data(), size);
    in_packets_.pop_back();
    return zx::ok(size);
  }

  size_t PeekPacketSize() override { return in_packets_.empty() ? 0 : in_packets_.back().size(); }

  // Send a packet over the transport.
  zx::status<> Send(std::string_view packet) override {
    out_packets_.push_back(std::string(packet.data(), packet.size()));
    return zx::ok();
  }

 private:
  Packets in_packets_;
  Packets out_packets_;
};

void CheckPacketsEqual(const Packets& lhs, const Packets& rhs) {
  ASSERT_EQ(lhs.size(), rhs.size());
  for (size_t i = 0; i < lhs.size(); i++) {
    ASSERT_EQ(lhs[i], rhs[i]);
  }
}

TEST(FastbootTest, NoPacket) {
  Fastboot fastboot(0x40000);
  TestTransport transport;
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  Packets expected_packets = {};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST(FastbootTest, GetVarMaxDownloadSize) {
  Fastboot fastboot(0x40000);
  const char command[] = "getvar:max-download-size";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  Packets expected_packets = {"OKAY0x00040000"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
}

TEST(FastbootTest, GetVarUnknownVariable) {
  Fastboot fastboot(0x40000);
  const char command[] = "getvar:unknown";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, GetVarNotEnoughArgument) {
  Fastboot fastboot(0x40000);
  const char command[] = "getvar";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, UnknownCommand) {
  Fastboot fastboot(0x40000);
  const char command[] = "Unknown";
  TestTransport transport;
  transport.AddInPacket(command, strlen(command));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

}  // namespace

}  // namespace fastboot
