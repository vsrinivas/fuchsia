// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt-transport-uart.h"

#include <fuchsia/hardware/serial/c/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <zxtest/zxtest.h>

namespace {

class FakeSerialImpl : public ddk::SerialImplAsyncProtocol<FakeSerialImpl> {
 public:
  FakeSerialImpl() : proto_({&serial_impl_async_protocol_ops_, this}) {}

  const serial_impl_async_protocol_t* proto() const { return &proto_; }
  bool enabled() const { return enabled_; }

  zx_status_t SerialImplAsyncGetInfo(serial_port_info_t* out_info) {
    out_info->serial_class = fuchsia_hardware_serial_Class_BLUETOOTH_HCI;
    return ZX_OK;
  }

  zx_status_t SerialImplAsyncConfig(uint32_t baud_rate, uint32_t flags) { return ZX_OK; }

  zx_status_t SerialImplAsyncEnable(bool enable) {
    enabled_ = enable;
    return ZX_OK;
  }

  void SerialImplAsyncReadAsync(serial_impl_async_read_async_callback callback, void* cookie) {}

  void SerialImplAsyncWriteAsync(const void* buf_buffer, size_t buf_size,
                                 serial_impl_async_write_async_callback callback, void* cookie) {}

  void SerialImplAsyncCancelAll() {}

 private:
  serial_impl_async_protocol_t proto_;
  bool enabled_{false};
};

TEST(BtTransportUartTest_, InitNoProtocolParent) {
  bt_transport_uart::BtTransportUart device(fake_ddk::kFakeParent);
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, device.Init());
}

class BtUartTransportTest : public zxtest::Test {
 public:
  BtUartTransportTest() {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_SERIAL_IMPL_ASYNC,
                    *reinterpret_cast<const fake_ddk::Protocol*>(serial_impl_.proto())};
    ddk_.SetProtocols(std::move(protocols));
  }

  fake_ddk::Bind& ddk() { return ddk_; }
  FakeSerialImpl& serial_impl() { return serial_impl_; }

 private:
  fake_ddk::Bind ddk_;
  FakeSerialImpl serial_impl_;
};

TEST_F(BtUartTransportTest, Init) {
  ASSERT_FALSE(serial_impl().enabled());
  bt_transport_uart::BtTransportUart device(fake_ddk::kFakeParent);
  ASSERT_EQ(ZX_OK, device.Init());
  ASSERT_TRUE(serial_impl().enabled());
}

TEST_F(BtUartTransportTest, DdkLifetime) {
  bt_transport_uart::BtTransportUart device(fake_ddk::kFakeParent);
  ASSERT_EQ(ZX_OK, device.Init());
  device.DdkAsyncRemove();
  EXPECT_TRUE(ddk().Ok());

  device.DdkRelease();
  EXPECT_TRUE(ddk().Ok());
}

TEST_F(BtUartTransportTest, HasExpectedProtocols) {
  bt_transport_uart::BtTransportUart device(fake_ddk::kFakeParent);
  ASSERT_EQ(ZX_OK, device.Init());

  ddk::AnyProtocol proto1{nullptr, nullptr};
  auto status = device.DdkGetProtocol(ZX_PROTOCOL_BT_HCI, &proto1);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_NE(nullptr, proto1.ctx);
  ASSERT_NE(nullptr, proto1.ops);

  ddk::AnyProtocol proto2{nullptr, nullptr};
  status = device.DdkGetProtocol(ZX_PROTOCOL_SERIAL_IMPL_ASYNC, &proto2);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_NE(nullptr, proto2.ctx);
  ASSERT_NE(nullptr, proto2.ops);

  ddk::AnyProtocol proto3{nullptr, nullptr};
  status = device.DdkGetProtocol(ZX_PROTOCOL_NAND, &proto3);
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status);
  ASSERT_EQ(nullptr, proto3.ctx);
  ASSERT_EQ(nullptr, proto3.ops);
}

TEST_F(BtUartTransportTest, OpenCommandChannel) {
  ASSERT_FALSE(serial_impl().enabled());
  bt_transport_uart::BtTransportUart device(fake_ddk::kFakeParent);
  ASSERT_EQ(ZX_OK, device.Init());

  zx::channel c1;
  zx::channel c2;
  auto status = zx::channel::create(0, &c1, &c2);
  ASSERT_EQ(ZX_OK, status);

  status = device.BtHciOpenCommandChannel(std::move(c1));
  ASSERT_EQ(ZX_OK, status);
}

TEST_F(BtUartTransportTest, OpenAclDataChannel) {
  ASSERT_FALSE(serial_impl().enabled());
  bt_transport_uart::BtTransportUart device(fake_ddk::kFakeParent);
  ASSERT_EQ(ZX_OK, device.Init());

  zx::channel c1;
  zx::channel c2;
  auto status = zx::channel::create(0, &c1, &c2);
  ASSERT_EQ(ZX_OK, status);

  status = device.BtHciOpenAclDataChannel(std::move(c1));
  ASSERT_EQ(ZX_OK, status);
}

TEST_F(BtUartTransportTest, OpenSnoopChannel) {
  ASSERT_FALSE(serial_impl().enabled());
  bt_transport_uart::BtTransportUart device(fake_ddk::kFakeParent);
  ASSERT_EQ(ZX_OK, device.Init());

  zx::channel c1;
  zx::channel c2;
  auto status = zx::channel::create(0, &c1, &c2);
  ASSERT_EQ(ZX_OK, status);

  status = device.BtHciOpenSnoopChannel(std::move(c1));
  ASSERT_EQ(ZX_OK, status);
}
}  // namespace
