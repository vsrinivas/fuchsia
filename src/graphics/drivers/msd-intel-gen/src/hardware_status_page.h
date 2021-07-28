// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_STATUS_PAGE_H
#define HARDWARE_STATUS_PAGE_H

#include "magma_util/macros.h"
#include "types.h"

// Base class for global and per-process hw status pages.
// There is a global HWSP for each engine command streamer, and a per-process
// HWSP for each context.  The layout of GHWSP and PPHWSP differs.
// The HardwareStatusPage doesn't own it's backing memory because the PPHWSP
// is part of the context buffer.
class HardwareStatusPage {
 public:
  class Owner {
   public:
    virtual void* hardware_status_page_cpu_addr(EngineCommandStreamerId id) = 0;
  };

  HardwareStatusPage(Owner* owner, EngineCommandStreamerId id)
      : owner_(owner), engine_command_streamer_id_(id) {}

  void* hardware_status_page_cpu_addr() {
    return owner_->hardware_status_page_cpu_addr(engine_command_streamer_id_);
  }

 private:
  Owner* owner_;
  EngineCommandStreamerId engine_command_streamer_id_;
};

// The Global hardware status page.
class GlobalHardwareStatusPage : public HardwareStatusPage {
 public:
  GlobalHardwareStatusPage(Owner* owner, EngineCommandStreamerId id)
      : HardwareStatusPage(owner, id) {}

  void write_sequence_number(uint32_t val) {
    write_general_purpose_offset(val, kSequenceNumberOffset);
  }

  uint32_t read_sequence_number() { return read_general_purpose_offset(kSequenceNumberOffset); }

  // from intel-gfx-prm-osrc-kbl-vol02d-commandreference-structures.pdf pp.284-286
  static constexpr uint32_t kGeneralPurposeStartOffset = 48 * sizeof(uint32_t);
  static constexpr uint32_t kGeneralPurposeEndOffset = 1023 * sizeof(uint32_t);

  static constexpr uint32_t kSequenceNumberOffset = kGeneralPurposeStartOffset;

 private:
  void write_general_purpose_offset(uint32_t val, uint32_t offset) {
    DASSERT((offset & 0x3) == 0);
    DASSERT(offset >= kGeneralPurposeStartOffset && offset <= kGeneralPurposeEndOffset);
    reinterpret_cast<uint32_t*>(hardware_status_page_cpu_addr())[offset >> 2] = val;
  }

  uint32_t read_general_purpose_offset(uint32_t offset) {
    DASSERT((offset & 0x3) == 0);
    DASSERT(offset >= kGeneralPurposeStartOffset && offset <= kGeneralPurposeEndOffset);
    return reinterpret_cast<uint32_t*>(hardware_status_page_cpu_addr())[offset >> 2];
  }
};

// Unused
class PerProcessHardwareStatusPage : public HardwareStatusPage {};

#endif  // HARDWARE_STATUS_PAGE_H
