#include "alc5663.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/errors.h>

#include <cassert>
#include <memory>
#include <unordered_map>
#include <vector>

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

// Fake ALC5663 hardware.
class FakeAlc5663 {
 public:
  enum class State {
    kUnknown,
    kReady,
  };

  FakeAlc5663()
      : fake_i2c_([this](uint16_t addr) { return OnRead(addr); },
                  [this](uint16_t addr, uint16_t data) { OnWrite(addr, data); }) {
    // Setup some register defaults.
    registers_.resize(kNumRegisters);
    registers_.at(VendorIdReg::kAddress) = VendorIdReg::kVendorRealtek;
  }

  // Install an override allowing a custom callback to be issued when a given
  // I2C bus address is accessed.
  //
  // Read callbacks should return a 16-bit value that will be passed back over
  // the I2C bus. They may call this->ReadRegister() if required.
  //
  // Write callbacks will receive a 16-bit data value. The callback should
  // call this->WriteRegister() if the value needs to actually be written.
  void InstallReadOverride(uint16_t address, std::function<uint16_t()> callback) {
    read_overrides_[address] = std::move(callback);
  }
  void InstallWriteOverride(uint16_t address, std::function<void(uint16_t)> callback) {
    write_overrides_[address] = std::move(callback);
  }

  // GetProto() exposes an I2C device, which is how the driver communicates to the real
  // hardware. In this case, this fake is on the other side of the I2C device.
  i2c_protocol_t GetProto() { return fake_i2c_.GetProto(); }

  State state() const { return state_; }

  // Write the given data to the given register.
  //
  // Typically, writes will be carried out by the driver via the I2C interface. This
  // method allows test to directly poke at registers to set up tests.
  void WriteRegister(uint16_t addr, uint16_t data) {
    // Driver should not write to registers until we have been reset.
    if (state_ == State::kUnknown) {
      ZX_ASSERT(addr == ResetAndDeviceIdReg::kAddress);
    }

    // Writes to ResetAndDeviceIdReg cause a device reset.
    if (addr == ResetAndDeviceIdReg::kAddress) {
      state_ = State::kReady;
    }

    // Store the value.
    registers_.at(addr) = data;
  }

  // Read data from the given register.
  //
  // Typically, reads will be carried out by the driver via the I2C interface. This
  // method allows test to verify values of registers.
  uint16_t ReadRegister(uint16_t addr) const { return registers_.at(addr); }

 private:
  // Read via the I2C bus.
  uint16_t OnRead(uint16_t address) {
    // Respect any overrides in place.
    auto it = read_overrides_.find(address);
    if (it != read_overrides_.end()) {
      return it->second();
    }

    // Otherwise, perform a normal read.
    return ReadRegister(address);
  }

  // Write via the I2C bus.
  void OnWrite(uint16_t address, uint16_t data) {
    // Respect any overrides in place.
    auto it = write_overrides_.find(address);
    if (it != write_overrides_.end()) {
      it->second(data);
      return;
    }

    // Otherwise, perform a normal read.
    WriteRegister(address, data);
  }

  static constexpr int kNumRegisters = 0x400;

  FakeI2c<uint16_t, uint16_t> fake_i2c_;
  State state_ = State::kUnknown;
  std::vector<uint16_t> registers_;

  std::unordered_map<uint16_t, std::function<uint16_t()>> read_overrides_;
  std::unordered_map<uint16_t, std::function<void(uint16_t)>> write_overrides_;
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

TEST(Alc5663, InvalidVendor) {
  FakeAlc5663Hardware hardware = CreateFakeAlc5663();

  // Setup override to return invalid vendor.
  hardware.codec->InstallReadOverride(VendorIdReg::kAddress, []() { return 0xbad; });

  // Create device.
  Alc5663Device* device;
  EXPECT_EQ(Alc5663Device::Bind(hardware.parent, &device), ZX_ERR_NOT_SUPPORTED);
}

}  // namespace
}  // namespace audio::alc5663
