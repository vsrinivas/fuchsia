// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial-ppp.h"

#include <fuchsia/net/ppp/llcpp/fidl.h>
#include <lib/zx/socket.h>
#include <zircon/errors.h>

#include <fbl/span.h>
#include <zxtest/zxtest.h>

#include "lib/common/ppp.h"

namespace {

namespace fppp = llcpp::fuchsia::net::ppp;

class SerialPppHarness : public zxtest::Test {
 public:
  void SetUp() override {
    zx::socket driver_socket;
    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &driver_socket, &socket_));

    device_ = std::make_unique<ppp::SerialPpp>();
    ASSERT_OK(device_->Init());
    ASSERT_OK(device_->Enable(true, std::move(driver_socket)));

    device_->SetStatus(fppp::ProtocolType::IPV4, true);
    device_->SetStatus(fppp::ProtocolType::IPV6, true);
  }

  void TearDown() override {
    device_->WaitCallbacks();
    auto device = device_.release();
    device->DdkRelease();
  }

  ppp::SerialPpp& Device() { return *device_; }

  zx::socket& Socket() { return socket_; }

 private:
  std::unique_ptr<ppp::SerialPpp> device_;
  zx::socket socket_;
};

TEST_F(SerialPppHarness, DriverTx) {
  std::string information = "\x12\x34Hello\x7eworld!";
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  auto result = Device().Tx(fppp::ProtocolType::CONTROL, span);
  ASSERT_OK(result);
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x32, 0x34, 'H', 'e',  'l',  'l',  'o',
      0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd',  '!', 0xe2, 0x7d, 0x25, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * 2);
  size_t actual = 0;
  ASSERT_OK(Socket().read(0, buffer.data(), buffer.size(), &actual));
  ASSERT_EQ(actual, expect.size());
  ASSERT_BYTES_EQ(buffer.data(), expect.data(), expect.size());
}

TEST_F(SerialPppHarness, DriverTxEscapedFcs) {
  // I stumbled upon a string that has a flag sequence in its FCS. Thus this
  // test is able to exist.
  std::string information = "\x34\x12Hello\x7eworld!";
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  auto result = Device().Tx(fppp::ProtocolType::CONTROL, span);
  ASSERT_OK(result);
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x34, 0x7d, 0x32, 'H',  'e',  'l',  'l',  'o',  0x7d,
      0x5e, 'w',  'o',  'r',  'l',  'd',  '!',  0x7d, 0x5e, 0x7d, 0x26, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * 2);
  size_t actual = 0;
  ASSERT_OK(Socket().read(0, buffer.data(), buffer.size(), &actual));
  ASSERT_EQ(actual, expect.size());
  ASSERT_BYTES_EQ(buffer.data(), expect.data(), expect.size());
}

TEST_F(SerialPppHarness, DriverTxEscapedProtocol) {
  std::string information = "\x7d\x7eHello world!";
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  auto result = Device().Tx(fppp::ProtocolType::CONTROL, span);
  ASSERT_OK(result);
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x5d, 0x7d, 0x5e, 'H',  'e',  'l',  'l',
      'o',  ' ',  'w',  'o',  'r',  'l',  'd',  '!',  0x5b, 0xcd, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * 2);
  size_t actual = 0;
  ASSERT_OK(Socket().read(0, buffer.data(), buffer.size(), &actual));
  ASSERT_EQ(actual, expect.size());
  ASSERT_BYTES_EQ(buffer.data(), expect.data(), expect.size());
}

TEST_F(SerialPppHarness, DriverTxNormalIpv4) {
  std::string information = "Some Ipv4";
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  auto result = Device().Tx(fppp::ProtocolType::IPV4, span);
  ASSERT_OK(result);
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',  'm',
      'e',  ' ',  'I',  'p',  'v',  '4',  0xae, 0xdf, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * 2);
  size_t actual = 0;
  ASSERT_OK(Socket().read(0, buffer.data(), buffer.size(), &actual));
  ASSERT_EQ(actual, expect.size());
  ASSERT_BYTES_EQ(buffer.data(), expect.data(), expect.size());
}

TEST_F(SerialPppHarness, DriverTxEmpty) {
  std::string information;
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  auto result = Device().Tx(fppp::ProtocolType::IPV6, span);
  ASSERT_OK(result);
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * 2);
  size_t actual = 0;
  ASSERT_OK(Socket().read(0, buffer.data(), buffer.size(), &actual));
  ASSERT_EQ(actual, expect.size());
  ASSERT_BYTES_EQ(buffer.data(), expect.data(), expect.size());
}

TEST_F(SerialPppHarness, DriverRxSingleFrameNoProtocol) {
  std::string information = "\x80\x21Hello\x7eworld!";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x80, 0x21, 'H', 'e', 'l',  'l',  'o',
      0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd', '!', 0xf6, 0xe1, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::CONTROL, [information](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv4Control);
    ASSERT_EQ(received.size(), information.size());
    ASSERT_BYTES_EQ(received.data(), information.data(), information.size());
  });
}

TEST_F(SerialPppHarness, DriverRxSingleFrame) {
  std::string information = "Some Ipv4";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',  'm',
      'e',  ' ',  'I',  'p',  'v',  '4',  0xae, 0xdf, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::IPV4, [information](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv4);
    ASSERT_EQ(received.size(), information.size());
    ASSERT_BYTES_EQ(received.data(), information.data(), information.size());
  });
}

TEST_F(SerialPppHarness, DriverRxSingleFrameFiller) {
  std::string information = "Some Ipv4";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0x7e, 0x7e, 0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',  'm',
      'e',  ' ',  'I',  'p',  'v',  '4',  0xae, 0xdf, 0x7e, 0x7e, 0x7e, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::IPV4, [information](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv4);
    ASSERT_EQ(received.size(), information.size());
    ASSERT_BYTES_EQ(received.data(), information.data(), information.size());
  });
}

TEST_F(SerialPppHarness, DriverRxTwoJoinedFrames) {
  std::string information0 = "Some Ipv4";
  std::string information1 = "\x80\x21Hello\x7eworld!";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',  'm',  'e',  ' ',  'I', 'p',
      'v',  '4',  0xae, 0xdf, 0x7e, 0xff, 0x7d, 0x23, 0x80, 0x21, 'H',  'e',  'l', 'l',
      'o',  0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd',  '!',  0xf6, 0xe1, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::IPV4, [information0, information1](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;

    ASSERT_EQ(protocol, ppp::Protocol::Ipv4);

    ASSERT_EQ(received.size(), information0.size());
    ASSERT_BYTES_EQ(received.data(), information0.data(), information0.size());
  });

  Device().Rx(fppp::ProtocolType::CONTROL, [information1](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;

    ASSERT_EQ(protocol, ppp::Protocol::Ipv4Control);

    ASSERT_EQ(received.size(), information1.size());
    ASSERT_BYTES_EQ(received.data(), information1.data(), information1.size());
  });
}

TEST_F(SerialPppHarness, DriverRxTwoJoinedFramesQueued) {
  std::string information1 = "Some Ipv4";
  std::string information2 = "\x80\x21Hello\x7eworld!";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',  'm',  'e',  ' ',  'I', 'p',
      'v',  '4',  0xae, 0xdf, 0x7e, 0xff, 0x7d, 0x23, 0x80, 0x21, 'H',  'e',  'l', 'l',
      'o',  0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd',  '!',  0xf6, 0xe1, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::CONTROL, [information2](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;

    ASSERT_EQ(protocol, ppp::Protocol::Ipv4Control);

    ASSERT_EQ(received.size(), information2.size());
    ASSERT_BYTES_EQ(received.data(), information2.data(), information2.size());
  });

  Device().Rx(fppp::ProtocolType::IPV4, [information1](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;

    ASSERT_EQ(protocol, ppp::Protocol::Ipv4);

    ASSERT_EQ(received.size(), information1.size());
    ASSERT_BYTES_EQ(received.data(), information1.data(), information1.size());
  });
}

TEST_F(SerialPppHarness, DriverRxEmpty) {
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::IPV6, [&](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv6);
    ASSERT_EQ(received.size(), 0);
  });
}

TEST_F(SerialPppHarness, DriverRxBadProtocol) {
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x58, 0x52, 0xf0, 0x7e,
      0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::IPV6, [&](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv6);
    ASSERT_EQ(received.size(), 0);
  });
}

TEST_F(SerialPppHarness, DriverRxBadHeader) {
  const std::vector<uint8_t> serial_data = {
      0x7e, 0x7d, 0x20, 0x7d, 0x20, 0x7d, 0x20, 0x57, 0x52, 0xf0,
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::IPV6, [&](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv6);
    ASSERT_EQ(received.size(), 0);
  });
}

TEST_F(SerialPppHarness, DriverRxTooShort) {
  const std::vector<uint8_t> serial_data = {
      0x7e, 0x7d, 0x20, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
      0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::IPV6, [&](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv6);
    ASSERT_EQ(received.size(), 0);
  });
}

TEST_F(SerialPppHarness, DriverRxTooLong) {
  const std::vector<uint8_t> serial_data_1(1500 * 2 + 9);
  const std::vector<uint8_t> serial_data_2 = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data_1.data(), serial_data_1.size(), &actual));
  ASSERT_EQ(actual, serial_data_1.size());

  actual = 0;
  ASSERT_OK(Socket().write(0, serial_data_2.data(), serial_data_2.size(), &actual));
  ASSERT_EQ(actual, serial_data_2.size());

  Device().Rx(fppp::ProtocolType::IPV6, [&](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv6);
    ASSERT_EQ(received.size(), 0);
  });
}

TEST_F(SerialPppHarness, DriverRxBadFrameCheckSequence) {
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x7d, 0x20, 0x7d, 0x20,
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  Device().Rx(fppp::ProtocolType::IPV6, [&](auto result) {
    ASSERT_TRUE(result.is_ok());
    auto& response = result.value();
    auto& protocol = response.protocol;
    auto& received = response.information;
    ASSERT_EQ(protocol, ppp::Protocol::Ipv6);
    ASSERT_EQ(received.size(), 0);
  });
}

}  // namespace
