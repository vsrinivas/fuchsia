// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VDEC1_H_
#define VDEC1_H_

#include "decoder_core.h"

#include <assert.h>

// This core is used for decoding all formats that aren't VP9 or HEVC, including
// MPEG-2 and h.264.
class Vdec1 : public DecoderCore {
 public:
  Vdec1(Owner* owner) : owner_(owner) {}

  ~Vdec1() override {
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

 private:
  MmioRegisters* mmio() const { return owner_->mmio(); }

  Owner* owner_;
  bool powered_on_ = false;
  bool decoding_started_ = false;
};

#endif  // VDEC1_H_
