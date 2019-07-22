#include "alc5663.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/i2c.h>
#include <fbl/array.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/errors.h>
#include <zxtest/zxtest.h>

#include <cassert>
#include <memory>

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
      : fake_i2c_([this](uint8_t addr) { return OnRead(addr); },
                  [this](uint8_t addr, uint16_t data) { OnWrite(addr, data); }) {}

  i2c_protocol_t GetProto() { return fake_i2c_.GetProto(); }

  State state() const { return state_; }

 private:
  uint16_t OnRead(uint8_t addr) {
    // Driver should not access registers until we have been reset.
    if (state_ == State::kUnknown) {
      ZX_ASSERT(addr == ResetAndDeviceIdReg::kAddress);
    }

    return 0;
  }

  void OnWrite(uint8_t addr, uint16_t /*data*/) {
    // Driver should not access registers until we have been reset.
    if (state_ == State::kUnknown) {
      ZX_ASSERT(addr == ResetAndDeviceIdReg::kAddress);
    }

    // Writes to ResetAndDeviceIdReg cause a device reset.
    if (addr == ResetAndDeviceIdReg::kAddress) {
      state_ = State::kReady;
    }
  }

  FakeI2c<uint8_t, uint16_t> fake_i2c_;
  State state_ = State::kUnknown;
};

// Set up the fake DDK instance `ddk` to export the given I2C protocol.
void SetupI2cProtocol(fake_ddk::Bind* ddk, i2c_protocol_t protocol) {
  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
  protocols[0] = {ZX_PROTOCOL_I2C, {/*ops=*/protocol.ops, /*ctx=*/protocol.ctx}};
  ddk->SetProtocols(std::move(protocols));
}

TEST(Alc5663, BindUnbind) {
  fake_ddk::Bind fake_ddk;
  FakeAlc5663 fake_alc5663{};
  SetupI2cProtocol(&fake_ddk, fake_alc5663.GetProto());

  // Create channel.
  ddk::I2cChannel channel(fake_ddk::kFakeParent);
  ASSERT_TRUE(channel.is_valid());

  // Create device.
  fbl::AllocChecker ac;
  auto device =
      fbl::unique_ptr<Alc5663Device>(new (&ac) Alc5663Device(fake_ddk::kFakeParent, channel));
  ASSERT_TRUE(ac.check());

  // Bind.
  Alc5663Device* device_ptr = device.get();
  ASSERT_OK(Alc5663Device::Bind(std::move(device)));

  // Ensure the device was reset.
  EXPECT_EQ(fake_alc5663.state(), FakeAlc5663::State::kReady);

  // Shutdown
  device_ptr->DdkRemove();
  device_ptr->DdkRelease();
  EXPECT_TRUE(fake_ddk.Ok());
}

}  // namespace
}  // namespace audio::alc5663
