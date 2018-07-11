// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "tests/test_support.h"
#include "vp9_decoder.h"

class FakeDecoderCore : public DecoderCore {
 public:
  zx_status_t LoadFirmware(const uint8_t* data, uint32_t len) override {
    return ZX_ERR_NO_RESOURCES;
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
};

class FakeOwner : public VideoDecoder::Owner {
 public:
  FakeOwner() : dosbus_(dos_memory) {}

  DosRegisterIo* dosbus() override { return &dosbus_; }
  zx_handle_t bti() override { return ZX_HANDLE_INVALID; }
  DeviceType device_type() override { return DeviceType::kGXM; }
  FirmwareBlob* firmware_blob() override { return nullptr; }
  std::unique_ptr<CanvasEntry> ConfigureCanvas(io_buffer_t* io_buffer,
                                               uint32_t offset, uint32_t width,
                                               uint32_t height, uint32_t wrap,
                                               uint32_t blockmode) override {
    return nullptr;
  }
  void FreeCanvas(std::unique_ptr<CanvasEntry> canvas) override {}
  DecoderCore* core() override { return &core_; }

 private:
  uint32_t dos_memory[0x4000] = {};
  DosRegisterIo dosbus_;
  FakeDecoderCore core_;
};

class Vp9UnitTest {
 public:
  static void LoopFilter() {
    FakeOwner fake_owner;
    auto decoder = std::make_unique<Vp9Decoder>(&fake_owner);
    decoder->InitLoopFilter();
    // This should be the 32nd value written to this register.
    EXPECT_EQ(0x3fc13ebeu,
              HevcDblkCfg9::Get().ReadFrom(fake_owner.dosbus()).reg_value());
  }
};

TEST(Vp9UnitTest, LoopFilter) { Vp9UnitTest::LoopFilter(); }
