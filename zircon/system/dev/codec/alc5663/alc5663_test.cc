#include "alc5663.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/errors.h>

#include <cassert>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/i2c.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "alc5663_registers.h"
#include "fake_i2c.h"

namespace audio::alc5663 {
namespace {

// Fake ALC5663 device.
class FakeAlc5663 {
 public:
  enum class State {
    kUnknown,
    kReady,
  };

  FakeAlc5663()
      : fake_i2c_([this](uint16_t addr) { return OnRead(addr); },
                  [this](uint16_t addr, uint16_t data) { OnWrite(addr, data); }) {}

  i2c_protocol_t GetProto() { return fake_i2c_.GetProto(); }

  State state() const { return state_; }

 private:
  uint16_t OnRead(uint16_t addr) {
    // Driver should not access registers until we have been reset.
    if (state_ == State::kUnknown) {
      ZX_ASSERT(addr == ResetAndDeviceIdReg::kAddress);
    }

    return 0;
  }

  void OnWrite(uint16_t addr, uint16_t /*data*/) {
    // Driver should not access registers until we have been reset.
    if (state_ == State::kUnknown) {
      ZX_ASSERT(addr == ResetAndDeviceIdReg::kAddress);
    }

    // Writes to ResetAndDeviceIdReg cause a device reset.
    if (addr == ResetAndDeviceIdReg::kAddress) {
      state_ = State::kReady;
    }
  }

  FakeI2c<uint16_t, uint16_t> fake_i2c_;
  State state_ = State::kUnknown;
};

// Fake ALC5663 codec hardware and associated infrastructure.
struct FakeAlc5663Hardware {
  std::unique_ptr<fake_ddk::Bind> fake_ddk;
  zx_device_t* parent;  // Parent I2C bus.
  std::unique_ptr<FakeAlc5663> codec;
};

// Set up the fake DDK instance `ddk` to export the given I2C protocol.
FakeAlc5663Hardware CreateFakeAlc5663() {
  FakeAlc5663Hardware result{};

  // Create the fake DDK.
  result.fake_ddk = std::make_unique<fake_ddk::Bind>();

  // Create the fake hardware device.
  result.codec = std::make_unique<FakeAlc5663>();

  // The driver will attempt to bind to the device on an I2C bus.
  //
  // Set up a fake parent I2C bus which exposes to the driver a way to talk to
  // the fake hardware.
  i2c_protocol_t protocol = result.codec->GetProto();
  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
  protocols[0] = {ZX_PROTOCOL_I2C, {/*ops=*/protocol.ops, /*ctx=*/protocol.ctx}};
  result.fake_ddk->SetProtocols(std::move(protocols));

  // Expose the parent device.
  result.parent = fake_ddk::kFakeParent;

  return result;
}

TEST(Alc5663, BindUnbind) {
  FakeAlc5663Hardware hardware = CreateFakeAlc5663();

  // Create device.
  Alc5663Device* device;
  ASSERT_OK(Alc5663Device::Bind(hardware.parent, &device));

  // Ensure the device was reset.
  EXPECT_EQ(hardware.codec->state(), FakeAlc5663::State::kReady);

  // Shutdown
  device->DdkRemove();
  device->DdkRelease();
  EXPECT_TRUE(hardware.fake_ddk->Ok());
}

}  // namespace
}  // namespace audio::alc5663
