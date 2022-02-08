// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fastboot/fastboot.h>

#include <vector>

#include <zxtest/zxtest.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace fastboot {

namespace {

using Packets = std::vector<std::string>;

class TestTransport : public Transport {
 public:
  void AddInPacket(const void* data, size_t size) {
    const char* start = static_cast<const char*>(data);
    in_packets_.insert(in_packets_.begin(), std::string(start, start + size));
  }

  template <typename T>
  void AddInPacket(const T& container) {
    return AddInPacket(container.data(), container.size());
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

class FastbootDownloadTest : public zxtest::Test {
 public:
  void DownloadData(Fastboot& fastboot, const std::vector<uint8_t>& download_content) {
    std::string size_hex_str = fxl::StringPrintf("%08zx", download_content.size());

    std::string command = "download:" + size_hex_str;
    TestTransport transport;
    transport.AddInPacket(command);
    zx::status<> ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());
    std::vector<std::string> expected_packets = {
        "DATA" + size_hex_str,
    };
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
    ASSERT_EQ(fastboot.download_vmo_mapper_.size(), download_content.size());
    // start the download

    // Transmit the first half.
    std::vector<uint8_t> first_half(download_content.begin(),
                                    download_content.begin() + download_content.size() / 2);
    transport.AddInPacket(first_half);
    ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());
    // There should be no new response packet.
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
    ASSERT_BYTES_EQ(fastboot.download_vmo_mapper_.start(), first_half.data(), first_half.size());

    // Transmit the second half
    std::vector<uint8_t> second_half(download_content.begin() + first_half.size(),
                                     download_content.end());
    transport.AddInPacket(second_half);
    ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());
    expected_packets.push_back("OKAY");
    ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));
    ASSERT_BYTES_EQ(fastboot.download_vmo_mapper_.start(), download_content.data(),
                    download_content.size());
  }
};

namespace {

TEST_F(FastbootDownloadTest, DownloadSucceed) {
  Fastboot fastboot(0x40000);

  std::vector<uint8_t> download_content;
  for (size_t i = 0; i <= 0xff; i++) {
    download_content.push_back(static_cast<uint8_t>(i));
  }

  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
}

TEST_F(FastbootDownloadTest, DownloadCompleteResetState) {
  Fastboot fastboot(0x40000);

  std::vector<uint8_t> download_content;
  for (size_t i = 0; i <= std::numeric_limits<uint8_t>::max(); i++) {
    download_content.push_back(static_cast<uint8_t>(i));
  }

  // Test the download command twice. The second time is to test that Fastboot re-enter
  // the command waiting state after a complete download.
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));
}

TEST(FastbootTest, DownloadFailsOnUnexpectedAmountOfData) {
  Fastboot fastboot(0x40000);

  std::vector<uint8_t> download_content;
  for (size_t i = 0; i <= std::numeric_limits<uint8_t>::max(); i++) {
    download_content.push_back(static_cast<uint8_t>(i));
  }

  std::string size_hex_str = fxl::StringPrintf("%08zx", download_content.size());

  std::string command = "download:" + size_hex_str;
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  // Transmit the first half.
  std::vector<uint8_t> first_half(download_content.begin(),
                                  download_content.begin() + download_content.size() / 2);
  transport.AddInPacket(first_half);
  ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  // The second transmit sends the entire download, which will exceed expected size.
  transport.AddInPacket(download_content);
  ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  // Check that the last packet is a FAIL response
  ASSERT_EQ(transport.GetOutPackets().size(), 2ULL);
  ASSERT_EQ(transport.GetOutPackets().back().compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, DownloadFailsOnZeroSizeDownload) {
  Fastboot fastboot(0x40000);
  std::string command = "download:00000000";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, DownloadFailsOnNotEnoughArgument) {
  Fastboot fastboot(0x40000);
  std::string command = "download";
  TestTransport transport;
  transport.AddInPacket(command);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());
  const std::vector<std::string>& sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

}  // namespace

}  // namespace fastboot
