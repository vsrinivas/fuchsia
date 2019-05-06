// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "tests/test_support.h"
#include "vp9_decoder.h"

namespace {
class FakeDecoderCore : public DecoderCore {
 public:
  zx_status_t LoadFirmware(const uint8_t* data, uint32_t len) override {
    return ZX_OK;
  }
  void PowerOn() override {}
  void PowerOff() override {}
  void StartDecoding() override {}
  void StopDecoding() override {}
  void WaitForIdle() override {}
  void InitializeStreamInput(bool use_parser, uint32_t buffer_address,
                             uint32_t buffer_size) override {}
  void InitializeParserInput() override {}
  void InitializeDirectInput() override {}
  void UpdateWritePointer(uint32_t write_pointer) override {}
  uint32_t GetStreamInputOffset() override { return 0; }
  uint32_t GetReadOffset() override { return 0; }
};

class FakeOwner : public VideoDecoder::Owner {
 public:
  FakeOwner(DosRegisterIo* dosbus) : dosbus_(dosbus) {
    blob_.LoadFakeFirmwareForTesting(FirmwareBlob::FirmwareType::kVp9Mmu,
                                     nullptr, 0);
  }

  DosRegisterIo* dosbus() override { return dosbus_; }
  zx_handle_t bti() override { return ZX_HANDLE_INVALID; }
  DeviceType device_type() override { return DeviceType::kGXM; }
  FirmwareBlob* firmware_blob() override { return &blob_; }
  std::unique_ptr<CanvasEntry> ConfigureCanvas(io_buffer_t* io_buffer,
                                               uint32_t offset, uint32_t width,
                                               uint32_t height, uint32_t wrap,
                                               uint32_t blockmode) override {
    return nullptr;
  }
  DecoderCore* core() override { return &core_; }
  zx_status_t AllocateIoBuffer(io_buffer_t* buffer, size_t size,
                               uint32_t alignment_log2,
                               uint32_t flags) override {
    zx_status_t status = io_buffer_init(buffer, ZX_HANDLE_INVALID, size,
                                        flags & ~IO_BUFFER_CONTIG);
    if (status != ZX_OK)
      return status;
    if (flags & IO_BUFFER_CONTIG) {
      if (alignment_log2 == 0)
        alignment_log2 = 12;
      phys_map_start_ = fbl::round_up(phys_map_start_, 1ul << alignment_log2);
      buffer->phys = phys_map_start_;
      phys_map_start_ += size;
    }
    return ZX_OK;
  }
  bool IsDecoderCurrent(VideoDecoder* decoder) override { return true; }

 private:
  DosRegisterIo* dosbus_;
  FakeDecoderCore core_;
  uint64_t phys_map_start_ = 0x1000;
  FirmwareBlob blob_;
};

constexpr uint32_t kDosbusMemorySize = 0x10000;
}  // namespace

class Vp9UnitTest {
 public:
  static void LoopFilter() {
    auto dosbus_memory =
        fbl::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    memset(dosbus_memory.get(), 0, kDosbusMemorySize);
    mmio_buffer_t dosbus_mmio = {.vaddr = dosbus_memory.get(),
                                 .size = kDosbusMemorySize,
                                 .vmo = ZX_HANDLE_INVALID};
    DosRegisterIo dosbus(dosbus_mmio);
    FakeOwner fake_owner(&dosbus);
    auto decoder = std::make_unique<Vp9Decoder>(
        &fake_owner, Vp9Decoder::InputType::kSingleStream);
    decoder->InitLoopFilter();
    // This should be the 32nd value written to this register.
    EXPECT_EQ(0x3fc13ebeu,
              HevcDblkCfg9::Get().ReadFrom(fake_owner.dosbus()).reg_value());
  }

  static void InitializeMemory() {
    auto zeroed_memory =
        fbl::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    auto dosbus_memory =
        fbl::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    memset(zeroed_memory.get(), 0, kDosbusMemorySize);
    memset(dosbus_memory.get(), 0, kDosbusMemorySize);
    mmio_buffer_t dosbus_mmio = {.vaddr = dosbus_memory.get(),
                                 .size = kDosbusMemorySize,
                                 .vmo = ZX_HANDLE_INVALID};
    DosRegisterIo dosbus(dosbus_mmio);
    FakeOwner fake_owner(&dosbus);
    auto decoder = std::make_unique<Vp9Decoder>(
        &fake_owner, Vp9Decoder::InputType::kSingleStream);
    EXPECT_EQ(ZX_OK, decoder->InitializeBuffers());
    EXPECT_EQ(
        0, memcmp(dosbus_memory.get(), zeroed_memory.get(), kDosbusMemorySize));

    EXPECT_EQ(ZX_OK, decoder->InitializeHardware());
    EXPECT_NE(
        0, memcmp(dosbus_memory.get(), zeroed_memory.get(), kDosbusMemorySize));
    auto dosbus_memory_copy =
        fbl::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    memcpy(dosbus_memory_copy.get(), dosbus_memory.get(), kDosbusMemorySize);
    memset(dosbus_memory.get(), 0, kDosbusMemorySize);

    decoder->state_ = Vp9Decoder::DecoderState::kSwappedOut;

    EXPECT_EQ(ZX_OK, decoder->InitializeHardware());
    EXPECT_EQ(0, memcmp(dosbus_memory.get(), dosbus_memory_copy.get(),
                        kDosbusMemorySize));
  }
};

TEST(Vp9UnitTest, LoopFilter) { Vp9UnitTest::LoopFilter(); }
TEST(Vp9UnitTest, InitializeMemory) { Vp9UnitTest::InitializeMemory(); }
