#include "alc5663.h"

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <fbl/array.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <zircon/errors.h>
#include <zxtest/zxtest.h>

#include <memory>

namespace audio::alc5663 {
namespace {

// Set up the fake DDK instance `ddk` to export the given I2C protocol.
void SetupI2cProtocol(fake_ddk::Bind* ddk, mock_i2c::MockI2c* i2c) {
  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
  protocols[0] = {ZX_PROTOCOL_I2C, *reinterpret_cast<const fake_ddk::Protocol*>(i2c)};
  ddk->SetProtocols(std::move(protocols));
}

TEST(Alc5663, BindUnbind) {
  fake_ddk::Bind fake_ddk;
  mock_i2c::MockI2c i2c;
  SetupI2cProtocol(&fake_ddk, &i2c);

  Alc5663Device* device;
  ASSERT_OK(Alc5663Device::CreateAndBind(fake_ddk::kFakeParent, &device));
  device->DdkRemove();
  device->DdkRelease();
  EXPECT_TRUE(fake_ddk.Ok());
}

}  // namespace
}  // namespace audio::alc5663
