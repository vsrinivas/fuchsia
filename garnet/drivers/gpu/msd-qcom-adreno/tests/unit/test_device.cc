// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <map>

#include <gtest/gtest.h>
#include <helper/platform_device_helper.h>

#include "garnet/drivers/gpu/msd-qcom-adreno/src/instructions.h"
#include "garnet/drivers/gpu/msd-qcom-adreno/src/msd_qcom_device.h"

namespace {
// Intercept all accesses to the register IO space and record the writes.
class TestHook : public magma::RegisterIo::Hook {
 public:
  void Write32(uint32_t offset, uint32_t val) override { map_[offset] = val; }
  void Read32(uint32_t offset, uint32_t val) override {}
  void Read64(uint32_t offset, uint64_t val) override {}

  std::map<uint32_t, uint32_t> map_;
};
}  // namespace

class TestQcomDevice : public ::testing::Test {
 public:
  void CreateAndDestroy() {
    auto device = MsdQcomDevice::Create(GetTestDeviceHandle());
    ASSERT_TRUE(device);
    DLOG("Got chip id: 0x%x", device->GetChipId());
    DLOG("Got gmem size: 0x%x", device->GetGmemSize());

    uint64_t firmware_addr = device->firmware()->gpu_addr();
    EXPECT_EQ(firmware_addr, MsdQcomDevice::kSystemGpuAddrBase);
    DLOG("Got firmware addr: 0x%lx", firmware_addr);

    uint64_t ringbuffer_addr;
    EXPECT_TRUE(device->ringbuffer()->GetGpuAddress(&ringbuffer_addr));
    DLOG("Got ringbuffer addr: 0x%lx", ringbuffer_addr);
  }

  // Contains the expected set of register writes from hardware init,
  // as recorded below.
  static std::vector<std::pair<uint32_t, uint32_t>> sparse_register_dump;

  void HardwareInit() {
    auto device = std::make_unique<MsdQcomDevice>();
    EXPECT_TRUE(device->Init(GetTestDeviceHandle(), std::make_unique<TestHook>()));

    auto hook = static_cast<TestHook*>(device->register_io()->hook());
    EXPECT_EQ(hook->map_.size(), sparse_register_dump.size());
    for (auto& pair : sparse_register_dump) {
      auto iter = hook->map_.find(pair.first);
      bool found = (iter != hook->map_.end());
      EXPECT_TRUE(found);
      if (found) {
        uint32_t expected = pair.second;
        uint32_t actual = iter->second;
        EXPECT_EQ(expected, actual) << "Mismatch at offset 0x" << std::hex << pair.first;
      }
    }

    // Dump list of writes
    if (false) {
      for (auto iter : hook->map_) {
        DLOG("{ 0x%08x, 0x%08x },", iter.first, iter.second);
      }
    }
  }

  void RegisterWrite() {
    auto device = std::make_unique<MsdQcomDevice>();
    EXPECT_TRUE(device->Init(GetTestDeviceHandle(), nullptr));

    constexpr uint32_t kScratchRegAddr = 0x00000885 << 2;
    // Initialize register to something arbitrary
    device->register_io()->Write32(kScratchRegAddr, 123456789);

    uint32_t expected = 0xabbadada;
    Packet4::write(device->ringbuffer(), kScratchRegAddr >> 2, 0xabbadada);

    uint32_t tail = device->ringbuffer()->tail() / sizeof(uint32_t);
    device->FlushRingbuffer(tail);
    EXPECT_TRUE(device->WaitForIdleRingbuffer(tail));

    EXPECT_EQ(expected, device->register_io()->Read32(kScratchRegAddr));
  }

  void MemoryWrite() {
    auto device = std::make_unique<MsdQcomDevice>();
    EXPECT_TRUE(device->Init(GetTestDeviceHandle(), nullptr));

    auto buffer = std::shared_ptr<magma::PlatformBuffer>(
        magma::PlatformBuffer::Create(magma::page_size(), "test"));

    void* ptr;
    ASSERT_TRUE(buffer->MapCpu(&ptr));

    *reinterpret_cast<uint32_t*>(ptr) = 123456789;

    constexpr uint32_t kScratchRegAddr = 0x00000885 << 2;
    uint32_t expected = 0xabbadada;
    device->register_io()->Write32(kScratchRegAddr, expected);

    auto gpu_mapping = AddressSpace::MapBufferGpu(device->address_space(), buffer);
    ASSERT_TRUE(gpu_mapping);

    std::vector<uint32_t> packet{
        kScratchRegAddr >> 2,
        magma::lower_32_bits(gpu_mapping->gpu_addr()),
        magma::upper_32_bits(gpu_mapping->gpu_addr()),
    };
    Packet7::write(device->ringbuffer(), Packet7::OpCode::CpRegisterToMemory, packet);

    uint32_t tail = device->ringbuffer()->tail() / sizeof(uint32_t);
    device->FlushRingbuffer(tail);
    EXPECT_TRUE(device->WaitForIdleRingbuffer(tail));

    uint32_t value = *reinterpret_cast<uint32_t*>(ptr);
    EXPECT_EQ(expected, value);
  }
};

TEST_F(TestQcomDevice, CreateAndDestroy) { TestQcomDevice::CreateAndDestroy(); }

TEST_F(TestQcomDevice, HardwareInit) { TestQcomDevice::HardwareInit(); }

TEST_F(TestQcomDevice, RegisterWrite) { TestQcomDevice::RegisterWrite(); }

TEST_F(TestQcomDevice, MemoryWrite) { TestQcomDevice::MemoryWrite(); }

std::vector<std::pair<uint32_t, uint32_t>> TestQcomDevice::sparse_register_dump = {
    // { offset, value }
    {0x00000040, 0x00000003}, {0x0000007c, 0x401fffff}, {0x00001400, 0x00000001},
    {0x00002000, 0xfe008000}, {0x00002004, 0x00000000}, {0x00002008, 0x0800020c},
    {0x0000201c, 0x00000009}, {0x00002020, 0x00000001}, {0x000020c0, 0xfe000000},
    {0x000020c4, 0x00000000}, {0x0000213c, 0x00000003}, {0x00002140, 0x01440600},
    {0x00002144, 0x8008ae50}, {0x00002148, 0x804c9624}, {0x0000214c, 0x80208630},
    {0x00002150, 0x80049e70}, {0x00002154, 0x861c9e78}, {0x00002158, 0xa040f000},
    {0x0000215c, 0x000cfc00}, {0x00002160, 0x8000050e}, {0x00002164, 0x0000050f},
    {0x00002168, 0x80000510}, {0x0000216c, 0x13e40000}, {0x00002170, 0x00280501},
    {0x00002174, 0x01100511}, {0x00002178, 0x80380e00}, {0x0000217c, 0x80008e00},
    {0x00002180, 0x803c8e50}, {0x00002184, 0x8000be02}, {0x00002188, 0xc7ccbe20},
    {0x0000218c, 0x82080800}, {0x00002190, 0x802008a0}, {0x00002194, 0x806408ab},
    {0x00002198, 0x81340900}, {0x0000219c, 0x81d8098d}, {0x000021a0, 0x00100980},
    {0x000021a4, 0x8000a630}, {0x00002304, 0x8040362c}, {0x00002308, 0x010000c0},
    {0x0000230c, 0x00000080}, {0x00002340, 0x00000000}, {0x00002634, 0x00000001},
    {0x00003804, 0x00400000}, {0x00003814, 0xffffffc0}, {0x00003818, 0x0001ffff},
    {0x0000381c, 0xfffff000}, {0x00003820, 0x0001ffff}, {0x00003824, 0xfffff000},
    {0x00003828, 0x0001ffff}, {0x0000382c, 0xff000000}, {0x00003830, 0x00000000},
    {0x00003834, 0xff0fffff}, {0x00003838, 0x00000000}, {0x0000385c, 0x00000004},
    {0x00003860, 0x00000804}, {0x00003864, 0x00000001}, {0x0000c0a8, 0x00000009},
    {0x00023820, 0x00000004}, {0x00027800, 0x00180000}, {0x0002b808, 0x00000004},
    {0x0002d810, 0x00000004}, {0x0003d000, 0x00000000}, {0x0003e000, 0x00000000},
    {0x0003e004, 0x00000000}, {0x0003e008, 0x00000000}, {0x0003e00c, 0x00000000},
};
