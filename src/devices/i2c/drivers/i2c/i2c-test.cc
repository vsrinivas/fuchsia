// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c.h"

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

#include "lib/ddk/metadata.h"
#include "src/devices/i2c/drivers/i2c/i2c-child.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {
namespace fi2c = fuchsia_hardware_i2c::wire;

class FakeI2cImpl;
using DeviceType = ddk::Device<FakeI2cImpl>;

class FakeI2cImpl : public DeviceType,
                    public ddk::I2cImplProtocol<FakeI2cImpl, ddk::base_protocol> {
 public:
  explicit FakeI2cImpl(zx_device_t* parent, std::vector<uint8_t> metadata)
      : DeviceType(parent), metadata_(std::move(metadata)) {
    ASSERT_OK(DdkAdd("fake-i2c-impl"));
    zxdev()->SetMetadata(DEVICE_METADATA_I2C_CHANNELS, metadata_.data(), metadata_.size());
  }

  static FakeI2cImpl* Create(zx_device_t* parent, std::vector<fi2c::I2CChannel> channels) {
    fidl::Arena<> arena;
    fi2c::I2CBusMetadata metadata(arena);

    auto channels_view = fidl::VectorView<fi2c::I2CChannel>::FromExternal(channels);
    metadata.set_channels(arena, channels_view);

    fidl::unstable::OwnedEncodedMessage<fi2c::I2CBusMetadata> encoded(
        fidl::internal::WireFormatVersion::kV2, &metadata);
    ZX_ASSERT(encoded.ok());
    auto message = encoded.GetOutgoingMessage().CopyBytes();
    std::vector<uint8_t> bytes(message.size());
    memcpy(bytes.data(), message.data(), message.size());

    auto impl = new FakeI2cImpl(parent, std::move(bytes));
    return impl;
  }
  uint32_t I2cImplGetBusBase() { return 0; }
  uint32_t I2cImplGetBusCount() { return 1; }
  zx_status_t I2cImplGetMaxTransferSize(uint32_t bus_id, uint64_t* out_size) {
    *out_size = 64;
    return ZX_OK;
  }
  zx_status_t I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate) { return ZX_OK; }

  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void DdkRelease() { delete this; }

 private:
  void AddMetadata() {}
  std::vector<uint8_t> metadata_;
};

fi2c::I2CChannel MakeChannel(fidl::AnyArena& arena, uint32_t bus_id, uint16_t addr) {
  fi2c::I2CChannel ret(arena);
  ret.set_address(addr);
  ret.set_bus_id(bus_id);
  return ret;
}

}  // namespace

class I2cMetadataTest : public zxtest::Test {
 public:
  I2cMetadataTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    loop_.StartThread();
    fake_root_ = MockDevice::FakeRootParent();
  }

  void TearDown() override {
    for (auto& device : fake_root_->children()) {
      device_async_remove(device.get());
    }

    mock_ddk::ReleaseFlaggedDevices(fake_root_.get());
  }

 protected:
  async::Loop loop_;
  std::shared_ptr<zx_device> fake_root_;
};

TEST_F(I2cMetadataTest, ProvidesMetadataToChildren) {
  std::vector<fi2c::I2CChannel> channels;
  fidl::Arena<> arena;
  channels.emplace_back(MakeChannel(arena, 0, 0xa));
  channels.emplace_back(MakeChannel(arena, 0, 0xb));

  auto impl = FakeI2cImpl::Create(fake_root_.get(), std::move(channels));
  impl->zxdev()->SetDispatcher(loop_.dispatcher());
  // Make the fake I2C driver.
  ASSERT_OK(i2c::I2cDevice::Create(nullptr, impl->zxdev()));

  // Check the number of devices we have makes sense.
  ASSERT_EQ(impl->zxdev()->child_count(), 1);
  zx_device_t* i2c = impl->zxdev()->GetLatestChild();
  // There should be two devices per channel, one for Banjo and one for FIDL.
  ASSERT_EQ(i2c->child_count(), 4);

  uint32_t banjo_protocols = 0;

  for (auto& child : i2c->children()) {
    uint16_t expected_addr = 0xff;
    for (const auto& prop : child->GetProperties()) {
      if (prop.id == BIND_I2C_ADDRESS) {
        expected_addr = static_cast<uint16_t>(prop.value);
      }
    }

    i2c_protocol_t proto;
    if (device_get_protocol(child.get(), ZX_PROTOCOL_I2C, &proto) == ZX_OK) {
      banjo_protocols++;
    }

    auto decoded =
        ddk::GetEncodedMetadata<fi2c::I2CChannel>(child.get(), DEVICE_METADATA_I2C_DEVICE);
    ASSERT_TRUE(decoded.is_ok());
    ASSERT_EQ(decoded->PrimaryObject()->address(), expected_addr);
  }

  // Half of the child devices should have Banjo protocols, the others should not.
  EXPECT_EQ(banjo_protocols, 2);
}
