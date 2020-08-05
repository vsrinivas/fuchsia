// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_HEVCDEC_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_HEVCDEC_H_

#include <assert.h>

#include "decoder_core.h"

// Used for both HEVC and VP9 video.
class HevcDec : public DecoderCore {
 public:
  HevcDec(Owner* owner) : owner_(owner) {}

  ~HevcDec() override {
    assert(!powered_on_);
    assert(!decoding_started_);
  }

  // DecoderCore implementation.
  [[nodiscard]] std::optional<InternalBuffer> LoadFirmwareToBuffer(const uint8_t* data,
                                                                   uint32_t len) override;
  [[nodiscard]] zx_status_t LoadFirmware(const uint8_t* data, uint32_t len) override;
  [[nodiscard]] zx_status_t LoadFirmware(InternalBuffer& buffer) override;
  void PowerOn() override;
  void PowerOff() override;
  void StartDecoding() override;
  void StopDecoding() override;
  void WaitForIdle() override;
  void InitializeStreamInput(bool use_parser, uint32_t buffer_address,
                             uint32_t buffer_size) override;
  void InitializeParserInput() override;
  void InitializeDirectInput() override;
  void UpdateWriteOffset(uint32_t write_offset) override;
  void UpdateWritePointer(uint32_t write_pointer) override;
  uint32_t GetStreamInputOffset() override;
  uint32_t GetReadOffset() override;
  zx_status_t InitializeInputContext(InputContext* context, bool is_secure) override;
  zx_status_t SaveInputContext(InputContext* context) override;
  zx_status_t RestoreInputContext(InputContext* context) override;

 private:
  MmioRegisters* mmio() const { return owner_->mmio(); }

  Owner* owner_;
  bool powered_on_ = false;
  bool decoding_started_ = false;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_HEVCDEC_H_
