// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>
#include <mmio-ptr/fake.h>

#include "amlogic-video.h"
#include "tests/test_support.h"
#include "vdec1.h"

namespace {

class FakeOwner : public DecoderCore::Owner {
 public:
  FakeOwner(MmioRegisters* mmio, AmlogicVideo* video) : mmio_(mmio), video_(video) {}

  MmioRegisters* mmio() override { return mmio_; }
  void UngateClocks() override{};
  void GateClocks() override{};
  zx::unowned_bti bti() override { return video_->bti(); }
  DeviceType device_type() override { return DeviceType::kG12B; }
  fuchsia::sysmem::AllocatorSyncPtr& SysmemAllocatorSyncPtr() override {
    return video_->SysmemAllocatorSyncPtr();
  }
  void ToggleClock(ClockType type, bool enable) override {
    enable_clock_state_[static_cast<int>(type)] = enable;
  }
  bool enable_clock_state(ClockType type) const {
    return enable_clock_state_[static_cast<int>(type)];
  }

 private:
  MmioRegisters* mmio_;
  AmlogicVideo* video_;
  std::array<bool, static_cast<int>(ClockType::kMax)> enable_clock_state_{};
};

constexpr uint32_t kDosbusMemorySize = 0x10000;
constexpr uint32_t kAobusMemorySize = 0x10000;
constexpr uint32_t kDmcMemorySize = 0x10000;
constexpr uint32_t kHiuBusMemorySize = 0x10000;
}  // namespace

TEST(Vdec1UnitTest, PowerOn) {
  auto video = std::make_unique<AmlogicVideo>();
  ASSERT_TRUE(video);
  EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

  auto dosbus_memory = std::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
  memset(dosbus_memory.get(), 0, kDosbusMemorySize);
  mmio_buffer_t dosbus_mmio = {.vaddr = FakeMmioPtr(dosbus_memory.get()),
                               .size = kDosbusMemorySize,
                               .vmo = ZX_HANDLE_INVALID};
  DosRegisterIo dosbus(dosbus_mmio);

  auto aobus_memory = std::unique_ptr<uint32_t[]>(new uint32_t[kAobusMemorySize]);
  memset(aobus_memory.get(), 0, kAobusMemorySize);
  mmio_buffer_t aobus_mmio = {
      .vaddr = FakeMmioPtr(aobus_memory.get()), .size = kAobusMemorySize, .vmo = ZX_HANDLE_INVALID};
  AoRegisterIo aobus(aobus_mmio);

  auto dmc_memory = std::unique_ptr<uint32_t[]>(new uint32_t[kDmcMemorySize]);
  memset(dmc_memory.get(), 0, kDmcMemorySize);
  mmio_buffer_t dmc_mmio = {
      .vaddr = FakeMmioPtr(dmc_memory.get()), .size = kDmcMemorySize, .vmo = ZX_HANDLE_INVALID};
  DmcRegisterIo dmc(dmc_mmio);

  auto hiubus_memory = std::unique_ptr<uint32_t[]>(new uint32_t[kHiuBusMemorySize]);
  memset(hiubus_memory.get(), 0, kHiuBusMemorySize);
  mmio_buffer_t hiubus_mmio = {.vaddr = FakeMmioPtr(hiubus_memory.get()),
                               .size = kHiuBusMemorySize,
                               .vmo = ZX_HANDLE_INVALID};
  HiuRegisterIo hiubus(hiubus_mmio);

  auto mmio = std::unique_ptr<MmioRegisters>(
      new MmioRegisters{&dosbus, &aobus, &dmc, &hiubus, /*reset*/ nullptr,
                        /*parser*/ nullptr, /*demux*/ nullptr});

  FakeOwner fake_owner(mmio.get(), video.get());
  auto decoder = std::make_unique<Vdec1>(&fake_owner);

  HhiVdecClkCntl::Get().FromValue(0xffff0000).WriteTo(fake_owner.mmio()->hiubus);
  DosGclkEn::Get().FromValue(0xfffffc00).WriteTo(fake_owner.mmio()->dosbus);
  decoder->PowerOn();
  // confirm non vdec bits weren't touched
  EXPECT_EQ(0xffff0000,
            HhiVdecClkCntl::Get().ReadFrom(fake_owner.mmio()->hiubus).reg_value() & 0xffff0000);
  EXPECT_EQ(0xfffffc00, DosGclkEn::Get().ReadFrom(fake_owner.mmio()->dosbus).reg_value());
  EXPECT_TRUE(fake_owner.enable_clock_state(ClockType::kGclkVdec));
  EXPECT_TRUE(fake_owner.enable_clock_state(ClockType::kClkDos));

  auto before_time = zx::clock::get_monotonic();
  decoder->PowerOff();
  decoder->PowerOn();
  auto after_time = zx::clock::get_monotonic();
  EXPECT_LE(zx::msec(2), after_time - before_time);
}
