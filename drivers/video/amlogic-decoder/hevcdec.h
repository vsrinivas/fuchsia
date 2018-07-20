// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_HEVCDEC_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_HEVCDEC_H_

#include "decoder_core.h"

#include <assert.h>

// Used for both HEVC and VP9 video.
class HevcDec : public DecoderCore {
 public:
  HevcDec(Owner* owner) : owner_(owner) {}

  ~HevcDec() override {
    assert(!powered_on_);
    assert(!decoding_started_);
  }

  // DecoderCore implementation.
  zx_status_t LoadFirmware(const uint8_t* data, uint32_t len) override;
  void PowerOn() override;
  void PowerOff() override;
  void StartDecoding() override;
  void StopDecoding() override;
  void WaitForIdle() override;
  void InitializeStreamInput(bool use_parser, uint32_t buffer_address,
                             uint32_t buffer_size) override;
  void InitializeParserInput() override;
  void InitializeDirectInput() override;
  void UpdateWritePointer(uint32_t write_pointer) override;
  uint32_t GetStreamInputOffset() override;
  zx_status_t InitializeInputContext(InputContext* context) override;
  void SaveInputContext(InputContext* context) override;
  void RestoreInputContext(InputContext* context) override;

 private:
  MmioRegisters* mmio() const { return owner_->mmio(); }

  Owner* owner_;
  bool powered_on_ = false;
  bool decoding_started_ = false;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_HEVCDEC_H_
