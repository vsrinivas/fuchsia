// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>
#include <mmio-ptr/fake.h>

#include "amlogic-video.h"
#include "tests/test_basic_client.h"
#include "tests/test_support.h"
#include "vp9_decoder.h"

namespace {
class FakeDecoderCore : public DecoderCore {
 public:
  std::optional<InternalBuffer> LoadFirmwareToBuffer(const uint8_t* data, uint32_t len) override {
    return {};
  }
  zx_status_t LoadFirmware(const uint8_t* data, uint32_t len) override { return ZX_OK; }
  zx_status_t LoadFirmware(InternalBuffer& buffer) override { return ZX_OK; }
  void PowerOn() override { powered_on_ = true; }
  void PowerOff() override { powered_on_ = false; }
  void StartDecoding() override {}
  void StopDecoding() override {}
  void WaitForIdle() override {}
  void InitializeStreamInput(bool use_parser, uint32_t buffer_address,
                             uint32_t buffer_size) override {}
  void InitializeParserInput() override {}
  void InitializeDirectInput() override {}
  void UpdateWriteOffset(uint32_t write_offset) override {}
  void UpdateWritePointer(uint32_t write_pointer) override {}
  uint32_t GetStreamInputOffset() override { return 0; }
  uint32_t GetReadOffset() override { return 0; }

  bool powered_on_ = false;
};

class FakeOwner : public VideoDecoder::Owner {
 public:
  FakeOwner(DosRegisterIo* dosbus, AmlogicVideo* video) : dosbus_(dosbus), video_(video) {
    blob_.LoadFakeFirmwareForTesting(FirmwareBlob::FirmwareType::kDec_Vp9_Mmu, nullptr, 0);
  }

  DosRegisterIo* dosbus() override { return dosbus_; }
  zx::unowned_bti bti() override { return video_->bti(); }
  DeviceType device_type() override { return DeviceType::kGXM; }
  FirmwareBlob* firmware_blob() override { return &blob_; }
  bool is_tee_available() override { return false; };
  zx_status_t TeeSmcLoadVideoFirmware(FirmwareBlob::FirmwareType index,
                                      FirmwareBlob::FirmwareVdecLoadMode vdec) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  [[nodiscard]] zx_status_t TeeVp9AddHeaders(zx_paddr_t page_phys_base, uint32_t before_size,
                                             uint32_t max_after_size,
                                             uint32_t* after_size) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  std::unique_ptr<CanvasEntry> ConfigureCanvas(io_buffer_t* io_buffer, uint32_t offset,
                                               uint32_t width, uint32_t height, uint32_t wrap,
                                               uint32_t blockmode) override {
    return nullptr;
  }
  DecoderCore* core() override { return &core_; }
  DecoderCore* vdec1_core() const override { return nullptr; }
  DecoderCore* hevc_core() const override { return &core_; }
  zx_status_t AllocateIoBuffer(io_buffer_t* buffer, size_t size, uint32_t alignment_log2,
                               uint32_t flags, const char* name) override {
    zx_status_t status = io_buffer_init(buffer, ZX_HANDLE_INVALID, size, flags & ~IO_BUFFER_CONTIG);
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
  fuchsia::sysmem::AllocatorSyncPtr& SysmemAllocatorSyncPtr() override {
    return video_->SysmemAllocatorSyncPtr();
  }

  bool IsDecoderCurrent(VideoDecoder* decoder) override { return true; }
  zx_status_t SetProtected(ProtectableHardwareUnit unit, bool protect) override {
    have_set_protected_ = true;
    return ZX_OK;
  }
  void TryToReschedule() override { EXPECT_TRUE(false); }
  Watchdog* watchdog() override {
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    return video_->watchdog();
  }

  bool have_set_protected() const { return have_set_protected_; }

 private:
  DosRegisterIo* dosbus_;
  AmlogicVideo* video_;
  mutable FakeDecoderCore core_;
  uint64_t phys_map_start_ = 0x1000;
  FirmwareBlob blob_;
  bool have_set_protected_ = false;
};

constexpr uint32_t kDosbusMemorySize = 0x10000;
}  // namespace

class Vp9UnitTest {
 public:
  static void LoopFilter() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);
    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    auto dosbus_memory = std::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    memset(dosbus_memory.get(), 0, kDosbusMemorySize);
    mmio_buffer_t dosbus_mmio = {.vaddr = FakeMmioPtr(dosbus_memory.get()),
                                 .size = kDosbusMemorySize,
                                 .vmo = ZX_HANDLE_INVALID};
    DosRegisterIo dosbus(dosbus_mmio);
    FakeOwner fake_owner(&dosbus, video.get());
    TestBasicClient client;
    auto decoder = std::make_unique<Vp9Decoder>(&fake_owner, &client,
                                                Vp9Decoder::InputType::kSingleStream, false, false);
    decoder->InitLoopFilter();
    // This should be the 32nd value written to this register.
    EXPECT_EQ(0x3fc13ebeu, HevcDblkCfg9::Get().ReadFrom(fake_owner.dosbus()).reg_value());
  }

  static void InitializeMemory(bool use_compressed_output) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);
    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    auto zeroed_memory = std::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    auto dosbus_memory = std::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    memset(zeroed_memory.get(), 0, kDosbusMemorySize);
    memset(dosbus_memory.get(), 0, kDosbusMemorySize);
    mmio_buffer_t dosbus_mmio = {.vaddr = FakeMmioPtr(dosbus_memory.get()),
                                 .size = kDosbusMemorySize,
                                 .vmo = ZX_HANDLE_INVALID};
    DosRegisterIo dosbus(dosbus_mmio);
    FakeOwner fake_owner(&dosbus, video.get());
    TestBasicClient client;
    auto decoder = std::make_unique<Vp9Decoder>(
        &fake_owner, &client, Vp9Decoder::InputType::kSingleStream, use_compressed_output, false);
    EXPECT_EQ(ZX_OK, decoder->InitializeBuffers());
    EXPECT_EQ(0, memcmp(dosbus_memory.get(), zeroed_memory.get(), kDosbusMemorySize));
    EXPECT_FALSE(fake_owner.have_set_protected());
    EXPECT_TRUE(static_cast<FakeDecoderCore*>(fake_owner.hevc_core())->powered_on_);

    EXPECT_EQ(ZX_OK, decoder->InitializeHardware());
    EXPECT_NE(0, memcmp(dosbus_memory.get(), zeroed_memory.get(), kDosbusMemorySize));
    EXPECT_TRUE(fake_owner.have_set_protected());
    auto dosbus_memory_copy = std::unique_ptr<uint32_t[]>(new uint32_t[kDosbusMemorySize]);
    memcpy(dosbus_memory_copy.get(), dosbus_memory.get(), kDosbusMemorySize);
    memset(dosbus_memory.get(), 0, kDosbusMemorySize);

    decoder->state_ = Vp9Decoder::DecoderState::kSwappedOut;

    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      video->watchdog()->Cancel();
    }
    EXPECT_EQ(ZX_OK, decoder->InitializeHardware());
    EXPECT_EQ(0, memcmp(dosbus_memory.get(), dosbus_memory_copy.get(), kDosbusMemorySize));
  }
};

TEST(Vp9UnitTest, LoopFilter) { Vp9UnitTest::LoopFilter(); }
TEST(Vp9UnitTest, InitializeMemory) { Vp9UnitTest::InitializeMemory(false); }
TEST(Vp9UnitTest, InitializeMemoryCompressed) { Vp9UnitTest::InitializeMemory(true); }
