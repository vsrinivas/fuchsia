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

TEST(FastbootTest, FastbootContinueTest) {
  Fastboot fastboot;
  fastboot::TestTransport transport;
  transport.AddInPacket(std::string("continue"));
  zx::status<> ret = fastboot.ProcessPacket(&transport);
  ASSERT_TRUE(ret.is_ok());

  ASSERT_EQ(transport.GetOutPackets().size(), 1ULL);
  ASSERT_EQ(transport.GetOutPackets().back(), "OKAY");
  ASSERT_TRUE(fastboot.IsContinue());
}

TEST(FastbootTcpSessionTest, ExitOnFastbootContinue) {
  Fastboot fastboot;
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
  Fastboot fastboot;
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
  Fastboot fastboot;
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
  Fastboot fastboot;
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
  Fastboot fastboot;
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

}  // namespace

}  // namespace gigaboot
