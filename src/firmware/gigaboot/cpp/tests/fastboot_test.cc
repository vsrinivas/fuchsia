// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fastboot.h"

#include <lib/fastboot/test/test-transport.h>

#include <vector>

#include <gtest/gtest.h>

#include "mock_boot_service.h"
#include "utils.h"

// For "ABC"sv literal string view operator
using namespace std::literals;

namespace gigaboot {

namespace {

class TestTcpTransport : public TcpTransportInterface {
 public:
  // Add data to the input stream.
  void AddInputData(const void* data, size_t size) {
    const uint8_t* start = static_cast<const uint8_t*>(data);
    in_data_.insert(in_data_.end(), start, start + size);
  }

  // Add a fastboot packet (length-prefixed byte sequence) to input stream.
  void AddFastbootPacket(const void* data, uint64_t size) {
    uint64_t be_size = ToBigEndian(size);
    AddInputData(&be_size, sizeof(be_size));
    AddInputData(data, size);
  }

  bool Read(void* out, size_t size) override {
    if (offset_ + size > in_data_.size()) {
      return false;
    }

    memcpy(out, in_data_.data() + offset_, size);
    offset_ += size;
    return true;
  }

  bool Write(const void* data, size_t size) override {
    const uint8_t* start = static_cast<const uint8_t*>(data);
    out_data_.insert(out_data_.end(), start, start + size);
    return true;
  }

  const std::vector<uint8_t>& GetOutData() { return out_data_; }

  void PopOutput(size_t size) {
    ASSERT_GE(out_data_.size(), size);
    out_data_ = std::vector<uint8_t>(out_data_.begin() + size, out_data_.end());
  }

  void PopAndCheckOutput(std::string_view expected) {
    ASSERT_GE(out_data_.size(), expected.size());
    std::string_view actual(reinterpret_cast<char*>(out_data_.data()), expected.size());
    ASSERT_EQ(actual, expected);
    PopOutput(expected.size());
  }

 private:
  size_t offset_ = 0;
  std::vector<uint8_t> in_data_;
  std::vector<uint8_t> out_data_;
};

constexpr size_t kDownloadBufferSize = 1024;
uint8_t download_buffer[kDownloadBufferSize];

TEST(FastbootTest, FastbootContinueTest) {
  Fastboot fastboot(download_buffer);
  fastboot::TestTransport transport;
  transport.AddInPacket(std::string("continue"));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  ASSERT_EQ(transport.GetOutPackets().size(), 1ULL);
  ASSERT_EQ(transport.GetOutPackets().back(), "OKAY");
  ASSERT_TRUE(fastboot.IsContinue());
}

TEST(FastbootTcpSessionTest, ExitOnFastbootContinue) {
  Fastboot fastboot(download_buffer);
  TestTcpTransport transport;

  // Handshake message
  const char handshake_message[] = "FB01";
  transport.AddInputData(handshake_message, strlen(handshake_message));

  // fastboot continue
  const char fastboot_cmd_continue[] = "continue";
  transport.AddFastbootPacket(fastboot_cmd_continue, strlen(fastboot_cmd_continue));

  // Add another packet, which should not be processed.
  const char fastboot_cmd_not_processed[] = "not-processed";
  transport.AddFastbootPacket(fastboot_cmd_not_processed, strlen(fastboot_cmd_not_processed));

  FastbootTcpSession(transport, fastboot);

  // API should return the same "FB01"
  ASSERT_NO_FATAL_FAILURE(transport.PopAndCheckOutput("FB01"));

  // Continue should return "OKAY"
  ASSERT_NO_FATAL_FAILURE(
      transport.PopAndCheckOutput("\x00\x00\x00\x00\x00\x00\x00\x04"
                                  "OKAY"sv));

  // The 'not-processed' command packet should not be processed. We shouldn't get
  // any new data in the output. (in this case it will be a failure message if processed).
  ASSERT_TRUE(transport.GetOutData().empty());
}

TEST(FastbootTcpSessionTest, HandshakeFailsNotFB) {
  Fastboot fastboot(download_buffer);
  TestTcpTransport transport;

  // Handshake message
  const char handshake_message[] = "AC01";
  transport.AddInputData(handshake_message, strlen(handshake_message));

  FastbootTcpSession(transport, fastboot);

  // API should write the same "FB01" no matter what is received
  ASSERT_NO_FATAL_FAILURE(transport.PopAndCheckOutput("FB01"));

  // Nothing should have been written.
  ASSERT_TRUE(transport.GetOutData().empty());
}

TEST(FastbootTcpSessionTest, HandshakeFailsNotNumericVersion) {
  Fastboot fastboot(download_buffer);
  TestTcpTransport transport;

  // Handshake message
  const char handshake_message[] = "FBxx";
  transport.AddInputData(handshake_message, strlen(handshake_message));

  FastbootTcpSession(transport, fastboot);

  // API should write the same "FB01" no matter what is received
  ASSERT_NO_FATAL_FAILURE(transport.PopAndCheckOutput("FB01"));

  // Nothing should have been written.
  ASSERT_TRUE(transport.GetOutData().empty());
}

TEST(FastbootTcpSessionTest, ExitWhenNoMoreData) {
  Fastboot fastboot(download_buffer);
  TestTcpTransport transport;

  // Handshake message
  const char handshake_message[] = "FB01";
  transport.AddInputData(handshake_message, strlen(handshake_message));

  FastbootTcpSession(transport, fastboot);

  // API should returns the same "FB01"
  ASSERT_NO_FATAL_FAILURE(transport.PopAndCheckOutput("FB01"));

  // No more data should be written.
  ASSERT_TRUE(transport.GetOutData().empty());
}

TEST(FastbootTcpSessionTest, ExitOnCommandFailure) {
  Fastboot fastboot(download_buffer);
  TestTcpTransport transport;

  // Handshake message
  const char handshake_message[] = "FB01";
  transport.AddInputData(handshake_message, strlen(handshake_message));

  // fastboot continue
  const char fastboot_cmd_continue[] = "unknown-cmd";
  transport.AddFastbootPacket(fastboot_cmd_continue, strlen(fastboot_cmd_continue));

  FastbootTcpSession(transport, fastboot);

  // Check and skip handshake message
  ASSERT_NO_FATAL_FAILURE(transport.PopAndCheckOutput("FB01"));
  // Skips 8- bytes length prefix
  ASSERT_NO_FATAL_FAILURE(transport.PopOutput(8));
  ASSERT_NO_FATAL_FAILURE(transport.PopAndCheckOutput("FAIL"));
}

void CheckPacketsEqual(const fastboot::Packets& lhs, const fastboot::Packets& rhs) {
  ASSERT_EQ(lhs.size(), rhs.size());
  for (size_t i = 0; i < lhs.size(); i++) {
    ASSERT_EQ(lhs[i], rhs[i]);
  }
}

class FastbootFlashTest : public ::testing::Test {
 public:
  FastbootFlashTest()
      : image_device_({"path-A", "path-B", "path-C", "image"}),
        block_device_({"path-A", "path-B", "path-C"}, 1024) {
    stub_service_.AddDevice(&image_device_);

    // Add a block device for fastboot flash test.
    stub_service_.AddDevice(&block_device_);
    block_device_.InitializeGpt();
  }

  void AddPartition(const gpt_entry_t& new_entry) {
    block_device_.AddGptPartition(new_entry);
    block_device_.FinalizeGpt();
  }

  uint8_t* BlockDeviceStart() { return block_device_.fake_disk_io_protocol().contents(0).data(); }

  // A helper to download data to fastboot
  void DownloadData(Fastboot& fastboot, const std::vector<uint8_t>& download_content) {
    char download_command[fastboot::kMaxCommandPacketSize];
    snprintf(download_command, fastboot::kMaxCommandPacketSize, "download:%08zx",
             download_content.size());
    fastboot::TestTransport transport;
    transport.AddInPacket(std::string(download_command));
    zx::status<> ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());

    // Download
    transport.AddInPacket(download_content);
    ret = fastboot.ProcessPacket(&transport);
    ASSERT_TRUE(ret.is_ok());

    char expected_data_message[fastboot::kMaxCommandPacketSize];
    snprintf(expected_data_message, fastboot::kMaxCommandPacketSize, "DATA%08zx",
             download_content.size());
    CheckPacketsEqual(transport.GetOutPackets(), {
                                                     expected_data_message,
                                                     "OKAY",
                                                 });
  }

  MockStubService& stub_service() { return stub_service_; }
  Device& image_device() { return image_device_; }
  BlockDevice& block_device() { return block_device_; }

 private:
  MockStubService stub_service_;
  Device image_device_;
  BlockDevice block_device_;
};

TEST(FastbootTest, GetVarNotEnoughArgument) {
  Fastboot fastboot(download_buffer);
  fastboot::TestTransport transport;

  transport.AddInPacket(std::string("getvar"));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  auto sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST(FastbootTest, GetVarUknown) {
  Fastboot fastboot(download_buffer);
  fastboot::TestTransport transport;

  transport.AddInPacket(std::string("getvar:non-existing"));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  auto sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, GetVarMaxDownloadSize) {
  auto cleanup = SetupEfiGlobalState(stub_service(), image_device());

  Fastboot fastboot(download_buffer);
  fastboot::TestTransport transport;

  transport.AddInPacket(std::string("getvar:max-download-size"));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  fastboot::Packets expected_packets = {"OKAY0x00000400"};
  CheckPacketsEqual(transport.GetOutPackets(), expected_packets);
}

TEST_F(FastbootFlashTest, FlashPartition) {
  auto cleanup = SetupEfiGlobalState(stub_service(), image_device());

  // Add a gpt entry for zircon_a
  gpt_entry_t entry{{}, {}, kGptFirstUsableBlocks, kGptFirstUsableBlocks + 5, 0, {}};
  SetGptEntryName(GPT_ZIRCON_A_NAME, entry);
  AddPartition(entry);

  Fastboot fastboot(download_buffer);

  // Download some data to flash to the partition.
  std::vector<uint8_t> download_content;
  for (size_t i = 0; i <= 0xff; i++) {
    download_content.push_back(static_cast<uint8_t>(i));
  }
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  fastboot::TestTransport transport;

  transport.AddInPacket(std::string("flash:") + GPT_ZIRCON_A_NAME);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  fastboot::Packets expected_packets = {"OKAY"};
  ASSERT_NO_FATAL_FAILURE(CheckPacketsEqual(transport.GetOutPackets(), expected_packets));

  uint8_t* partition_start = BlockDeviceStart() + entry.first * kBlockSize;
  ASSERT_TRUE(memcmp(partition_start, download_content.data(), download_content.size()) == 0);
}

TEST_F(FastbootFlashTest, FlashPartitionFailedToFindGptDevice) {
  // Set the current device to one whose parent is not the added block device for the test.
  Device image({"non-parent-path", "image"});
  stub_service().AddDevice(&image);
  auto cleanup = SetupEfiGlobalState(stub_service(), image);

  // Add a gpt entry for zircon_a
  gpt_entry_t entry{{}, {}, kGptFirstUsableBlocks, kGptFirstUsableBlocks + 5, 0, {}};
  SetGptEntryName(GPT_ZIRCON_A_NAME, entry);
  AddPartition(entry);

  Fastboot fastboot(download_buffer);

  // Download some data to flash to the partition.
  std::vector<uint8_t> download_content(128, 0);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  fastboot::TestTransport transport;

  transport.AddInPacket(std::string("flash:") + GPT_ZIRCON_A_NAME);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_error());

  // Should fail while searching for gpt device.
  auto sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

TEST_F(FastbootFlashTest, FlashPartitionFailedToLoadGptDevice) {
  auto cleanup = SetupEfiGlobalState(stub_service(), image_device());
  // Clear the gpt.
  memset(BlockDeviceStart(), 0, block_device().total_blocks() * kBlockSize);

  Fastboot fastboot(download_buffer);

  // Download some data to flash to the partition.
  std::vector<uint8_t> download_content(128, 0);
  ASSERT_NO_FATAL_FAILURE(DownloadData(fastboot, download_content));

  fastboot::TestTransport transport;

  transport.AddInPacket(std::string("flash:") + GPT_ZIRCON_A_NAME);
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_error());

  // Should fail while loading gpt.
  auto sent_packets = transport.GetOutPackets();
  ASSERT_EQ(sent_packets.size(), 1ULL);
  ASSERT_EQ(sent_packets[0].compare(0, 4, "FAIL"), 0);
}

}  // namespace

}  // namespace gigaboot
