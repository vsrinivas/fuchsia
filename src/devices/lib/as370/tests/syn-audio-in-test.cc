// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/shareddma.h>
#include <soc/as370/as370-dma.h>
#include <soc/as370/syn-audio-in.h>
#include <zxtest/zxtest.h>

bool operator==(const shared_dma_protocol_t& a, const shared_dma_protocol_t& b) { return true; }
bool operator==(const dma_notify_t& a, const dma_notify_t& b) { return true; }

class CicFilterTest : public CicFilter {
 public:
  explicit CicFilterTest() : CicFilter() {}
  uint32_t Filter(uint32_t index, void* input, uint32_t input_size, void* output,
                  uint32_t input_total_channels, uint32_t input_channel,
                  uint32_t output_total_channels, uint32_t output_channel,
                  uint32_t multiplier_shift) {
    return 4;  // mock decodes 4 bytes.
  }
};

class SynAudioInDeviceTest : public SynAudioInDevice {
 public:
  static std::unique_ptr<SynAudioInDeviceTest> Create(ddk::MockSharedDma* dma) {
    static fbl::Array<ddk_mock::MockMmioReg> unused_mocks =
        fbl::Array(new ddk_mock::MockMmioReg[1], 1);
    static ddk_mock::MockMmioRegRegion unused_region(unused_mocks.data(), sizeof(uint32_t), 1);
    ddk::MmioBuffer b1(unused_region.GetMmioBuffer());
    ddk::MmioBuffer b2(unused_region.GetMmioBuffer());
    ddk::MmioBuffer b3(unused_region.GetMmioBuffer());

    fbl::AllocChecker ac;
    auto dev = std::unique_ptr<SynAudioInDeviceTest>(new (&ac) SynAudioInDeviceTest(
        std::move(b1), std::move(b2), std::move(b3), dma->GetProto()));
    if (!ac.check()) {
      return nullptr;
    }
    return dev;
  }
  SynAudioInDeviceTest(ddk::MmioBuffer mmio_global, ddk::MmioBuffer mmio_avio,
                       ddk::MmioBuffer mmio_i2s, ddk::SharedDmaProtocolClient dma)
      : SynAudioInDevice(std::move(mmio_global), std::move(mmio_avio), std::move(mmio_i2s),
                         std::move(dma)) {
    cic_filter_ = std::make_unique<CicFilterTest>();
    dma_buffer_size_[0] = 0x10;
    if (kNumberOfDmas > 1) {
      dma_buffer_size_[1] = 0x20;
    }
  }
  bool HasAtLeastTwoDmas() { return kNumberOfDmas >= 2; }
};

namespace audio {

TEST(SynapticsAudioInTest, ProcessDmaSimple) {
  ddk::MockSharedDma dma;
  auto dev = SynAudioInDeviceTest::Create(&dma);

  dma.ExpectGetTransferSize(4, DmaId::kDmaIdPdmW0);

  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x8, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);

  dev->ProcessDma(0);
  dma.VerifyAndClear();
}

TEST(SynapticsAudioInTest, ProcessDmaWarp) {
  ddk::MockSharedDma dma;
  auto dev = SynAudioInDeviceTest::Create(&dma);

  dma.ExpectGetTransferSize(4, DmaId::kDmaIdPdmW0);

  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x8, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x0, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x8, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);

  dev->ProcessDma(0);
  dma.VerifyAndClear();
}

TEST(SynapticsAudioInTest, ProcessDmaIrregular) {
  ddk::MockSharedDma dma;
  auto dev = SynAudioInDeviceTest::Create(&dma);

  dma.ExpectGetTransferSize(4, DmaId::kDmaIdPdmW0);

  dma.ExpectGetBufferPosition(0x8, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);

  dev->ProcessDma(0);
  dma.VerifyAndClear();
}

TEST(SynapticsAudioInTest, ProcessDmaOverflow) {
  ddk::MockSharedDma dma;
  auto dev = SynAudioInDeviceTest::Create(&dma);

  dma.ExpectGetTransferSize(4, DmaId::kDmaIdPdmW0);

  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW0);

  dev->ProcessDma(0);
  dma.VerifyAndClear();
}

TEST(SynapticsAudioInTest, ProcessDmaPdm0AndPdm1) {
  ddk::MockSharedDma dma;
  auto dev = SynAudioInDeviceTest::Create(&dma);

  if (!dev->HasAtLeastTwoDmas()) {
    return;
  }

  // every call to ProcessDma gets transfer size from PDM0.
  dma.ExpectGetTransferSize(4, DmaId::kDmaIdPdmW0);
  dma.ExpectGetTransferSize(4, DmaId::kDmaIdPdmW0);
  dma.ExpectGetTransferSize(4, DmaId::kDmaIdPdmW0);

  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x8, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x0, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW0);

  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0x8, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0x10, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0x14, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0x18, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0x1c, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0x0, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW1);
  dma.ExpectGetBufferPosition(0x4, DmaId::kDmaIdPdmW1);

  dma.ExpectGetBufferPosition(0x8, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);
  dma.ExpectGetBufferPosition(0xc, DmaId::kDmaIdPdmW0);

  dev->ProcessDma(0);
  dev->ProcessDma(1);
  dev->ProcessDma(0);

  dma.VerifyAndClear();
}

TEST(SynapticsAudioInTest, FifoDepth) {
  ddk::MockSharedDma dma;
  auto dev = SynAudioInDeviceTest::Create(&dma);
  // 16384 PDM DMA transfer size as used for PDM, generates 1024 samples at 48KHz 16 bits.
  dma.ExpectGetTransferSize(16384, DmaId::kDmaIdPdmW0);

  // 12288 = 3 channels x 1024 samples per DMA x 2 bytes per sample x 2 for ping-pong.
  ASSERT_EQ(dev->FifoDepth(), 12288);
  dma.VerifyAndClear();
}

}  // namespace audio
