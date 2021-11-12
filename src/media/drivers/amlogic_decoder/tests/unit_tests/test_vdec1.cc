// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/fdio/directory.h>
#include <lib/mmio-ptr/fake.h>

#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "amlogic-video.h"
#include "tests/test_support.h"
#include "vdec1.h"

namespace amlogic_decoder {
namespace test {
namespace {

fuchsia::sysmem::AllocatorSyncPtr ConnectToSysmem() {
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  EXPECT_EQ(ZX_OK, status);
  return sysmem_allocator;
}

class FakeOwner : public DecoderCore::Owner {
 public:
  explicit FakeOwner(MmioRegisters* mmio) : mmio_(mmio) {
    zx_status_t status = fake_bti_create(bti_.reset_and_get_address());
    EXPECT_EQ(ZX_OK, status);
    allocator_ = ConnectToSysmem();
  }

  void set_device_type(DeviceType type) { device_type_ = type; }

  MmioRegisters* mmio() override { return mmio_; }
  void UngateClocks() override { clocks_gated_ = false; }
  void GateClocks() override { clocks_gated_ = true; }
  zx::unowned_bti bti() override { return zx::unowned_bti(bti_); }
  DeviceType device_type() override { return device_type_; }
  fuchsia::sysmem::AllocatorSyncPtr& SysmemAllocatorSyncPtr() override { return allocator_; }
  void ToggleClock(ClockType type, bool enable) override {
    enable_clock_state_[static_cast<int>(type)] = enable;
  }
  bool enable_clock_state(ClockType type) const {
    return enable_clock_state_[static_cast<int>(type)];
  }

  bool clocks_gated() const { return clocks_gated_; }

 private:
  zx::bti bti_;
  bool clocks_gated_ = true;
  fuchsia::sysmem::AllocatorSyncPtr allocator_;
  MmioRegisters* mmio_;
  std::array<bool, static_cast<int>(ClockType::kMax)> enable_clock_state_{};
  DeviceType device_type_ = DeviceType::kG12B;
};

constexpr uint32_t kDosbusMemorySize = 0x10000;
constexpr uint32_t kAobusMemorySize = 0x10000;
constexpr uint32_t kDmcMemorySize = 0x10000;
constexpr uint32_t kHiuBusMemorySize = 0x10000;
}  // namespace

class Vdec1UnitTest : public testing::Test {
 public:
  void SetUp() override {
    dosbus_memory_ = std::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    memset(dosbus_memory_.get(), 0, kDosbusMemorySize);
    mmio_buffer_t dosbus_mmio = {.vaddr = FakeMmioPtr(dosbus_memory_.get()),
                                 .size = kDosbusMemorySize,
                                 .vmo = ZX_HANDLE_INVALID};
    dosbus_.emplace(dosbus_mmio);

    aobus_memory_ = std::unique_ptr<uint32_t[]>(new uint32_t[kAobusMemorySize]);
    memset(aobus_memory_.get(), 0, kAobusMemorySize);
    mmio_buffer_t aobus_mmio = {.vaddr = FakeMmioPtr(aobus_memory_.get()),
                                .size = kAobusMemorySize,
                                .vmo = ZX_HANDLE_INVALID};
    aobus_.emplace(aobus_mmio);

    dmc_memory_ = std::unique_ptr<uint32_t[]>(new uint32_t[kDmcMemorySize]);
    memset(dmc_memory_.get(), 0, kDmcMemorySize);
    mmio_buffer_t dmc_mmio = {
        .vaddr = FakeMmioPtr(dmc_memory_.get()), .size = kDmcMemorySize, .vmo = ZX_HANDLE_INVALID};
    dmc_.emplace(dmc_mmio);

    hiubus_memory_ = std::unique_ptr<uint32_t[]>(new uint32_t[kHiuBusMemorySize]);
    memset(hiubus_memory_.get(), 0, kHiuBusMemorySize);
    mmio_buffer_t hiubus_mmio = {.vaddr = FakeMmioPtr(hiubus_memory_.get()),
                                 .size = kHiuBusMemorySize,
                                 .vmo = ZX_HANDLE_INVALID};
    hiubus_.emplace(hiubus_mmio);

    mmio_ = std::unique_ptr<MmioRegisters>(
        new MmioRegisters{&*dosbus_, &*aobus_, &*dmc_, &*hiubus_, /*reset*/ nullptr,
                          /*parser*/ nullptr, /*demux*/ nullptr});
  }

 protected:
  std::unique_ptr<uint32_t[]> dosbus_memory_;
  std::unique_ptr<uint32_t[]> aobus_memory_;
  std::unique_ptr<uint32_t[]> dmc_memory_;
  std::unique_ptr<uint32_t[]> hiubus_memory_;
  std::optional<DosRegisterIo> dosbus_;
  std::optional<AoRegisterIo> aobus_;
  std::optional<DmcRegisterIo> dmc_;
  std::optional<HiuRegisterIo> hiubus_;

  std::unique_ptr<MmioRegisters> mmio_;
};

TEST_F(Vdec1UnitTest, PowerOn) {
  FakeOwner fake_owner(mmio_.get());
  auto decoder = std::make_unique<Vdec1>(&fake_owner);

  HhiVdecClkCntl::Get().FromValue(0xffff0000).WriteTo(fake_owner.mmio()->hiubus);
  DosGclkEn::Get().FromValue(0xfffffc00).WriteTo(fake_owner.mmio()->dosbus);
  decoder->PowerOn();
  // confirm non vdec bits weren't touched
  EXPECT_EQ(0xffff0000,
            HhiVdecClkCntl::Get().ReadFrom(fake_owner.mmio()->hiubus).reg_value() & 0xffff0000);
  EXPECT_EQ(0xfffffc00, DosGclkEn::Get().ReadFrom(fake_owner.mmio()->dosbus).reg_value());
  EXPECT_TRUE(fake_owner.enable_clock_state(ClockType::kGclkVdec));
  EXPECT_FALSE(fake_owner.clocks_gated());

  decoder->PowerOff();
  EXPECT_TRUE(fake_owner.clocks_gated());
}

TEST_F(Vdec1UnitTest, PowerOnSm1) {
  FakeOwner fake_owner(mmio_.get());

  fake_owner.set_device_type(DeviceType::kSM1);
  auto decoder = std::make_unique<Vdec1>(&fake_owner);

  AoRtiGenPwrIso0::Get().FromValue(0xffffffff).WriteTo(fake_owner.mmio()->aobus);
  AoRtiGenPwrSleep0::Get().FromValue(0xffffffff).WriteTo(fake_owner.mmio()->aobus);
  HhiVdecClkCntl::Get().FromValue(0xffff0000).WriteTo(fake_owner.mmio()->hiubus);
  DosGclkEn::Get().FromValue(0xfffffc00).WriteTo(fake_owner.mmio()->dosbus);
  decoder->PowerOn();
  // confirm non vdec bits weren't touched
  EXPECT_EQ(0xffff0000,
            HhiVdecClkCntl::Get().ReadFrom(fake_owner.mmio()->hiubus).reg_value() & 0xffff0000);
  EXPECT_EQ(0xfffffc00, DosGclkEn::Get().ReadFrom(fake_owner.mmio()->dosbus).reg_value());
  EXPECT_EQ(0xffffffffu & ~2,
            AoRtiGenPwrIso0::Get().ReadFrom(fake_owner.mmio()->aobus).reg_value());
  EXPECT_EQ(0xffffffffu & ~2,
            AoRtiGenPwrSleep0::Get().ReadFrom(fake_owner.mmio()->aobus).reg_value());

  EXPECT_TRUE(fake_owner.enable_clock_state(ClockType::kGclkVdec));
  EXPECT_FALSE(fake_owner.clocks_gated());

  decoder->PowerOff();

  EXPECT_TRUE(fake_owner.clocks_gated());
  EXPECT_EQ(0xffffffffu, AoRtiGenPwrIso0::Get().ReadFrom(fake_owner.mmio()->aobus).reg_value());
  EXPECT_EQ(0xffffffffu, AoRtiGenPwrSleep0::Get().ReadFrom(fake_owner.mmio()->aobus).reg_value());
}

}  // namespace test
}  // namespace amlogic_decoder
