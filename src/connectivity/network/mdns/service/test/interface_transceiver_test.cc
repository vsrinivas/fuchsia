// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include <iomanip>
#include <iostream>

#include <gtest/gtest.h>

#include "lib/fostr/hex_dump.h"
#include "src/connectivity/network/mdns/service/mdns_interface_transceiver.h"

namespace mdns {
namespace test {

class MdnsInterfaceTransceiverTest : public MdnsInterfaceTransceiver {
 public:
  MdnsInterfaceTransceiverTest(inet::IpAddress address, const std::string& name, uint32_t index,
                               Media media)
      : MdnsInterfaceTransceiver(address, name, index, media) {}

  virtual ~MdnsInterfaceTransceiverTest() override {}

  // Set by |SendTo|.
  const void* send_to_buffer_{};
  size_t send_to_size_{};
  inet::SocketAddress send_to_address_{};

  // Dumps a golden for |SendTo|.
  void DumpSendToGolden() {
    FX_CHECK(send_to_buffer_ != nullptr);
    FX_CHECK(send_to_size_ != 0);

    std::cout << fostr::HexDump(send_to_buffer_, send_to_size_, 0) << "\n\n";

    std::cout << "  std::vector<uint8_t> expected_message = {";

    for (size_t i = 0; i < send_to_size_; ++i) {
      if (i % 12 == 0) {
        std::cout << "\n      ";
      }

      std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<uint16_t>(reinterpret_cast<const uint8_t*>(send_to_buffer_)[i])
                << std::dec << ", ";
    }

    std::cout << "};\n";
  }

 protected:
  // MdnsInterfaceTransceiver overrides.
  int SetOptionDisableMulticastLoop() override { return 0; }
  int SetOptionJoinMulticastGroup() override { return 0; }
  int SetOptionOutboundInterface() override { return 0; }
  int SetOptionUnicastTtl() override { return 0; }
  int SetOptionMulticastTtl() override { return 0; }
  int SetOptionFamilySpecific() override { return 0; }
  int Bind() override { return 0; }
  int SendTo(const void* buffer, size_t size, const inet::SocketAddress& address) override {
    send_to_buffer_ = buffer;
    send_to_size_ = size;
    send_to_address_ = address;
    return 0;
  }
};

// Constructs an |MdnsInterfaceTransceiverTest| and checks the values of its
// identifying properties.
TEST(InterfaceTransceiverTest, Construct) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inet::IpAddress nic_address(1, 2, 3, 4);
  std::string nic_name = "testnic";
  uint32_t nic_index = 1234;

  MdnsInterfaceTransceiverTest under_test(nic_address, nic_name, nic_index, Media::kWired);

  EXPECT_EQ(nic_address, under_test.address());
  EXPECT_EQ(nic_name, under_test.name());
  EXPECT_EQ(nic_index, under_test.index());
  EXPECT_EQ(Media::kWired, under_test.media());
}

// Sends a message containing no A or AAAA resources.
TEST(InterfaceTransceiverTest, SendSimpleMessage) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inet::IpAddress nic_address(1, 2, 3, 4);
  std::string nic_name = "testnic";
  uint32_t nic_index = 1234;

  inet::SocketAddress to_address(inet::IpAddress(4, 3, 2, 1), inet::IpPort::From_uint16_t(4321));

  MdnsInterfaceTransceiverTest under_test(nic_address, nic_name, nic_index, Media::kWired);

  auto ptr_resource = std::make_shared<DnsResource>("_test_name._whatever.", DnsType::kPtr);
  ptr_resource->time_to_live_ = 234;
  ptr_resource->ptr_.pointer_domain_name_ = DnsName("_test_ptr_name._whatever.");

  DnsMessage message;
  message.additionals_.push_back(ptr_resource);
  message.UpdateCounts();

  under_test.SendMessage(&message, to_address);
  EXPECT_NE(nullptr, under_test.send_to_buffer_);

  // 0000  00 00 00 00 00 00 00 00  00 00 00 01 0a 5f 74 65  ............._te
  // 0010  73 74 5f 6e 61 6d 65 09  5f 77 68 61 74 65 76 65  st_name._whateve
  // 0020  72 00 00 0c 00 01 00 00  00 ea 00 11 0e 5f 74 65  r............_te
  // 0030  73 74 5f 70 74 72 5f 6e  61 6d 65 c0 17           st_ptr_name..

  std::vector<uint8_t> expected_message = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0a,
      0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x09, 0x5f, 0x77,
      0x68, 0x61, 0x74, 0x65, 0x76, 0x65, 0x72, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00,
      0x00, 0x00, 0xea, 0x00, 0x11, 0x0e, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x70,
      0x74, 0x72, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0, 0x17};

  EXPECT_EQ(expected_message.size(), under_test.send_to_size_);
  EXPECT_EQ(0,
            memcmp(expected_message.data(), under_test.send_to_buffer_, under_test.send_to_size_));
  EXPECT_EQ(to_address, under_test.send_to_address_);
}

// Sends a message containing a leading A resource.
TEST(InterfaceTransceiverTest, SendLeadingA) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inet::IpAddress nic_address(1, 2, 3, 4);
  std::string nic_name = "testnic";
  uint32_t nic_index = 1234;

  inet::SocketAddress to_address(inet::IpAddress(4, 3, 2, 1), inet::IpPort::From_uint16_t(4321));

  MdnsInterfaceTransceiverTest under_test(nic_address, nic_name, nic_index, Media::kWired);

  auto a_resource = std::make_shared<DnsResource>("_test_a_name._whatever.", DnsType::kA);

  auto ptr_resource = std::make_shared<DnsResource>("_test_name._whatever.", DnsType::kPtr);
  ptr_resource->time_to_live_ = 234;
  ptr_resource->ptr_.pointer_domain_name_ = DnsName("_test_ptr_name._whatever.");

  DnsMessage message;
  message.additionals_.push_back(a_resource);
  message.additionals_.push_back(ptr_resource);
  message.UpdateCounts();

  under_test.SendMessage(&message, to_address);
  EXPECT_NE(nullptr, under_test.send_to_buffer_);

  // under_test.DumpSendToGolden();

  // 0000  00 00 00 00 00 00 00 00  00 00 00 02 0a 5f 74 65  ............._te
  // 0010  73 74 5f 6e 61 6d 65 09  5f 77 68 61 74 65 76 65  st_name._whateve
  // 0020  72 00 00 0c 00 01 00 00  00 ea 00 11 0e 5f 74 65  r............_te
  // 0030  73 74 5f 70 74 72 5f 6e  61 6d 65 c0 17 0c 5f 74  st_ptr_name..._t
  // 0040  65 73 74 5f 61 5f 6e 61  6d 65 c0 17 00 01 80 01  est_a_name......
  // 0050  00 00 00 78 00 04 01 02  03 04                    ...x......

  std::vector<uint8_t> expected_message = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0a, 0x5f, 0x74,
      0x65, 0x73, 0x74, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x09, 0x5f, 0x77, 0x68, 0x61, 0x74, 0x65,
      0x76, 0x65, 0x72, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x00, 0xea, 0x00, 0x11, 0x0e,
      0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x70, 0x74, 0x72, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x0c, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x61, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x00, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04,
  };

  EXPECT_EQ(expected_message.size(), under_test.send_to_size_);
  EXPECT_EQ(0,
            memcmp(expected_message.data(), under_test.send_to_buffer_, under_test.send_to_size_));
  EXPECT_EQ(to_address, under_test.send_to_address_);
}

// Sends a message containing leading A and AAAA resources.
TEST(InterfaceTransceiverTest, SendLeadingAAndAAAA) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inet::IpAddress nic_address(1, 2, 3, 4);
  std::string nic_name = "testnic";
  uint32_t nic_index = 1234;

  inet::SocketAddress to_address(inet::IpAddress(4, 3, 2, 1), inet::IpPort::From_uint16_t(4321));

  MdnsInterfaceTransceiverTest under_test(nic_address, nic_name, nic_index, Media::kWired);

  auto a_resource = std::make_shared<DnsResource>("_test_a_name._whatever.", DnsType::kA);

  auto aaaa_resource = std::make_shared<DnsResource>("_test_a_name._whatever.", DnsType::kAaaa);

  auto ptr_resource = std::make_shared<DnsResource>("_test_name._whatever.", DnsType::kPtr);
  ptr_resource->time_to_live_ = 234;
  ptr_resource->ptr_.pointer_domain_name_ = DnsName("_test_ptr_name._whatever.");

  DnsMessage message;
  message.additionals_.push_back(a_resource);
  message.additionals_.push_back(aaaa_resource);
  message.additionals_.push_back(ptr_resource);
  message.UpdateCounts();

  under_test.SendMessage(&message, to_address);
  EXPECT_NE(nullptr, under_test.send_to_buffer_);

  // under_test.DumpSendToGolden();

  // 0000  00 00 00 00 00 00 00 00  00 00 00 02 0a 5f 74 65  ............._te
  // 0010  73 74 5f 6e 61 6d 65 09  5f 77 68 61 74 65 76 65  st_name._whateve
  // 0020  72 00 00 0c 00 01 00 00  00 ea 00 11 0e 5f 74 65  r............_te
  // 0030  73 74 5f 70 74 72 5f 6e  61 6d 65 c0 17 0c 5f 74  st_ptr_name..._t
  // 0040  65 73 74 5f 61 5f 6e 61  6d 65 c0 17 00 01 80 01  est_a_name......
  // 0050  00 00 00 78 00 04 01 02  03 04                    ...x......

  std::vector<uint8_t> expected_message = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0a, 0x5f, 0x74,
      0x65, 0x73, 0x74, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x09, 0x5f, 0x77, 0x68, 0x61, 0x74, 0x65,
      0x76, 0x65, 0x72, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x00, 0xea, 0x00, 0x11, 0x0e,
      0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x70, 0x74, 0x72, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x0c, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x61, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x00, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04,
  };

  EXPECT_EQ(expected_message.size(), under_test.send_to_size_);
  EXPECT_EQ(0,
            memcmp(expected_message.data(), under_test.send_to_buffer_, under_test.send_to_size_));
  EXPECT_EQ(to_address, under_test.send_to_address_);
}

// Sends a message containing trailing A and AAAA resources.
TEST(InterfaceTransceiverTest, SendTrailingAAndAAAA) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inet::IpAddress nic_address(1, 2, 3, 4);
  std::string nic_name = "testnic";
  uint32_t nic_index = 1234;

  inet::SocketAddress to_address(inet::IpAddress(4, 3, 2, 1), inet::IpPort::From_uint16_t(4321));

  MdnsInterfaceTransceiverTest under_test(nic_address, nic_name, nic_index, Media::kWired);

  auto a_resource = std::make_shared<DnsResource>("_test_a_name._whatever.", DnsType::kA);

  auto aaaa_resource = std::make_shared<DnsResource>("_test_a_name._whatever.", DnsType::kAaaa);

  auto ptr_resource = std::make_shared<DnsResource>("_test_name._whatever.", DnsType::kPtr);
  ptr_resource->time_to_live_ = 234;
  ptr_resource->ptr_.pointer_domain_name_ = DnsName("_test_ptr_name._whatever.");

  DnsMessage message;
  message.additionals_.push_back(ptr_resource);
  message.additionals_.push_back(a_resource);
  message.additionals_.push_back(aaaa_resource);
  message.UpdateCounts();

  under_test.SendMessage(&message, to_address);
  EXPECT_NE(nullptr, under_test.send_to_buffer_);

  // under_test.DumpSendToGolden();

  // 0000  00 00 00 00 00 00 00 00  00 00 00 02 0a 5f 74 65  ............._te
  // 0010  73 74 5f 6e 61 6d 65 09  5f 77 68 61 74 65 76 65  st_name._whateve
  // 0020  72 00 00 0c 00 01 00 00  00 ea 00 11 0e 5f 74 65  r............_te
  // 0030  73 74 5f 70 74 72 5f 6e  61 6d 65 c0 17 0c 5f 74  st_ptr_name..._t
  // 0040  65 73 74 5f 61 5f 6e 61  6d 65 c0 17 00 01 80 01  est_a_name......
  // 0050  00 00 00 78 00 04 01 02  03 04                    ...x......

  std::vector<uint8_t> expected_message = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0a, 0x5f, 0x74,
      0x65, 0x73, 0x74, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x09, 0x5f, 0x77, 0x68, 0x61, 0x74, 0x65,
      0x76, 0x65, 0x72, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x00, 0xea, 0x00, 0x11, 0x0e,
      0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x70, 0x74, 0x72, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x0c, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x61, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x00, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04,
  };

  EXPECT_EQ(expected_message.size(), under_test.send_to_size_);
  EXPECT_EQ(0,
            memcmp(expected_message.data(), under_test.send_to_buffer_, under_test.send_to_size_));
  EXPECT_EQ(to_address, under_test.send_to_address_);
}

// Sends a message containing bracketing A and AAAA resources.
TEST(InterfaceTransceiverTest, SendBracketingAAndAAAA) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inet::IpAddress nic_address(1, 2, 3, 4);
  std::string nic_name = "testnic";
  uint32_t nic_index = 1234;

  inet::SocketAddress to_address(inet::IpAddress(4, 3, 2, 1), inet::IpPort::From_uint16_t(4321));

  MdnsInterfaceTransceiverTest under_test(nic_address, nic_name, nic_index, Media::kWired);

  auto a_resource = std::make_shared<DnsResource>("_test_a_name._whatever.", DnsType::kA);

  auto aaaa_resource = std::make_shared<DnsResource>("_test_a_name._whatever.", DnsType::kAaaa);

  auto ptr_resource = std::make_shared<DnsResource>("_test_name._whatever.", DnsType::kPtr);
  ptr_resource->time_to_live_ = 234;
  ptr_resource->ptr_.pointer_domain_name_ = DnsName("_test_ptr_name._whatever.");

  DnsMessage message;
  message.additionals_.push_back(a_resource);
  message.additionals_.push_back(ptr_resource);
  message.additionals_.push_back(aaaa_resource);
  message.UpdateCounts();

  under_test.SendMessage(&message, to_address);
  EXPECT_NE(nullptr, under_test.send_to_buffer_);

  // under_test.DumpSendToGolden();

  // 0000  00 00 00 00 00 00 00 00  00 00 00 02 0a 5f 74 65  ............._te
  // 0010  73 74 5f 6e 61 6d 65 09  5f 77 68 61 74 65 76 65  st_name._whateve
  // 0020  72 00 00 0c 00 01 00 00  00 ea 00 11 0e 5f 74 65  r............_te
  // 0030  73 74 5f 70 74 72 5f 6e  61 6d 65 c0 17 0c 5f 74  st_ptr_name..._t
  // 0040  65 73 74 5f 61 5f 6e 61  6d 65 c0 17 00 01 80 01  est_a_name......
  // 0050  00 00 00 78 00 04 01 02  03 04                    ...x......

  std::vector<uint8_t> expected_message = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0a, 0x5f, 0x74,
      0x65, 0x73, 0x74, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x09, 0x5f, 0x77, 0x68, 0x61, 0x74, 0x65,
      0x76, 0x65, 0x72, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x00, 0xea, 0x00, 0x11, 0x0e,
      0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x70, 0x74, 0x72, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x0c, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x61, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x00, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04,
  };

  EXPECT_EQ(expected_message.size(), under_test.send_to_size_);
  EXPECT_EQ(0,
            memcmp(expected_message.data(), under_test.send_to_buffer_, under_test.send_to_size_));
  EXPECT_EQ(to_address, under_test.send_to_address_);
}

// Sends a message containing a leading A resource with alternate address.
TEST(InterfaceTransceiverTest, SendLeadingAWithAlternate) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  inet::IpAddress nic_address(1, 2, 3, 4);
  inet::IpAddress alternate_address(1, 2);
  std::string nic_name = "testnic";
  uint32_t nic_index = 1234;

  inet::SocketAddress to_address(inet::IpAddress(4, 3, 2, 1), inet::IpPort::From_uint16_t(4321));

  MdnsInterfaceTransceiverTest under_test(nic_address, nic_name, nic_index, Media::kWired);
  under_test.SetAlternateAddress(alternate_address);

  auto a_resource = std::make_shared<DnsResource>("_test_a_name._whatever.", DnsType::kA);

  auto ptr_resource = std::make_shared<DnsResource>("_test_name._whatever.", DnsType::kPtr);
  ptr_resource->time_to_live_ = 234;
  ptr_resource->ptr_.pointer_domain_name_ = DnsName("_test_ptr_name._whatever.");

  DnsMessage message;
  message.additionals_.push_back(a_resource);
  message.additionals_.push_back(ptr_resource);
  message.UpdateCounts();

  under_test.SendMessage(&message, to_address);
  EXPECT_NE(nullptr, under_test.send_to_buffer_);

  // under_test.DumpSendToGolden();

  // 0000  00 00 00 00 00 00 00 00  00 00 00 03 0a 5f 74 65  ............._te
  // 0010  73 74 5f 6e 61 6d 65 09  5f 77 68 61 74 65 76 65  st_name._whateve
  // 0020  72 00 00 0c 00 01 00 00  00 ea 00 11 0e 5f 74 65  r............_te
  // 0030  73 74 5f 70 74 72 5f 6e  61 6d 65 c0 17 0c 5f 74  st_ptr_name..._t
  // 0040  65 73 74 5f 61 5f 6e 61  6d 65 c0 17 00 01 80 01  est_a_name......
  // 0050  00 00 00 78 00 04 01 02  03 04 c0 3d 00 1c 80 01  ...x.......=....
  // 0060  00 00 00 78 00 10 00 01  00 00 00 00 00 00 00 00  ...x............
  // 0070  00 00 00 00 00 02                                 ......

  std::vector<uint8_t> expected_message = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x0a, 0x5f, 0x74,
      0x65, 0x73, 0x74, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x09, 0x5f, 0x77, 0x68, 0x61, 0x74, 0x65,
      0x76, 0x65, 0x72, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x00, 0xea, 0x00, 0x11, 0x0e,
      0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x70, 0x74, 0x72, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x0c, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x5f, 0x61, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0xc0,
      0x17, 0x00, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04,
      0xc0, 0x3d, 0x00, 0x1c, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x10, 0x00, 0x01, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
  };

  EXPECT_EQ(expected_message.size(), under_test.send_to_size_);
  EXPECT_EQ(0,
            memcmp(expected_message.data(), under_test.send_to_buffer_, under_test.send_to_size_));
  EXPECT_EQ(to_address, under_test.send_to_address_);
}

}  // namespace test
}  // namespace mdns
