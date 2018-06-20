// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DECODER_CORE_H_
#define DECODER_CORE_H_

#include <zx/handle.h>

#include "registers.h"

struct MmioRegisters {
  DosRegisterIo* dosbus;
  AoRegisterIo* aobus;
  DmcRegisterIo* dmc;
  HiuRegisterIo* hiubus;
  ResetRegisterIo* reset;
};

class DecoderCore {
 public:
  class Owner {
   public:
    virtual zx_handle_t bti() = 0;
    virtual MmioRegisters* mmio() = 0;
    virtual void UngateClocks() = 0;
    virtual void GateClocks() = 0;
  };

  virtual ~DecoderCore() {}

  virtual zx_status_t LoadFirmware(const uint8_t* data, uint32_t len) = 0;
  virtual void PowerOn() = 0;
  virtual void PowerOff() = 0;
  virtual void StartDecoding() = 0;
  virtual void StopDecoding() = 0;
  virtual void WaitForIdle() = 0;
  virtual void InitializeStreamInput(bool use_parser, uint32_t buffer_address,
                                     uint32_t buffer_size) = 0;
  virtual void InitializeParserInput() = 0;
  virtual void InitializeDirectInput() = 0;
  // The write pointer points to just after the last thing that was written into
  // the stream buffer.
  virtual void UpdateWritePointer(uint32_t write_pointer) = 0;
  // This is the offset between the start of the stream buffer and the write
  // pointer.
  virtual uint32_t GetStreamInputOffset() = 0;
};

#endif  // DECODER_CORE_H_
