// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_STATUS_PAGE_H
#define HARDWARE_STATUS_PAGE_H

#include <optional>

#include "address_space.h"
#include "gpu_mapping.h"
#include "magma_util/macros.h"
#include "types.h"

// There is a global HWSP for each engine command streamer, and a per-process
// HWSP for each context.  The layout of GHWSP and PPHWSP differs.
class GlobalHardwareStatusPage {
 public:
  GlobalHardwareStatusPage(EngineCommandStreamerId id, std::unique_ptr<GpuMapping> mapping)
      : id_(id), mapping_(std::move(mapping)) {
    if (!mapping_->buffer()->platform_buffer()->MapCpu(&cpu_addr_)) {
      DASSERT(false);
      cpu_addr_ = 0;
    }
  }

  EngineCommandStreamerId id() { return id_; }

  uint64_t gpu_addr() { return mapping_->gpu_addr(); }

  void write_sequence_number(uint32_t val) {
    write_general_purpose_offset(val, kSequenceNumberOffset);
  }

  uint32_t read_sequence_number() { return read_general_purpose_offset(kSequenceNumberOffset); }

  void InitContextStatusGen12();

  // Reads all available context status entries; if there are any, then |idle_out| is set
  // according to the most recent status.  Updates |read_index|.
  void ReadContextStatus(uint64_t& read_index, std::optional<bool>* idle_out);
  void ReadContextStatusGen12(uint64_t& read_index, std::optional<bool>* idle_out);

  // from intel-gfx-prm-osrc-kbl-vol02d-commandreference-structures.pdf pp.284-286
  // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02d-commandreference-structures_0.pdf
  // p.600
  static constexpr uint32_t kContextStatusStartOffset = 16 * sizeof(uint32_t);
  static constexpr uint32_t kContextStatusEndOffsetGen12 = 39 * sizeof(uint32_t);
  static constexpr uint32_t kLastWrittenContextStatusOffsetGen12 = 47 * sizeof(uint32_t);
  static constexpr uint32_t kGeneralPurposeStartOffset = 48 * sizeof(uint32_t);
  static constexpr uint32_t kGeneralPurposeEndOffset = 1023 * sizeof(uint32_t);

  static constexpr uint64_t kStatusQwordsGen12 = 12;
  static_assert(kStatusQwordsGen12 * sizeof(uint64_t) ==
                kContextStatusEndOffsetGen12 - kContextStatusStartOffset + sizeof(uint32_t));

  // Our definitions
  static constexpr uint32_t kSequenceNumberOffset = kGeneralPurposeStartOffset;
  static constexpr uint32_t kScratchOffset = kSequenceNumberOffset + sizeof(uint64_t);

 private:
  void write_general_purpose_offset(uint32_t val, uint32_t offset) {
    DASSERT((offset & 0x3) == 0);
    DASSERT(offset >= kGeneralPurposeStartOffset && offset <= kGeneralPurposeEndOffset);
    reinterpret_cast<uint32_t*>(cpu_addr_)[offset >> 2] = val;
  }

  uint32_t read_general_purpose_offset(uint32_t offset) {
    DASSERT((offset & 0x3) == 0);
    DASSERT(offset >= kGeneralPurposeStartOffset && offset <= kGeneralPurposeEndOffset);
    return reinterpret_cast<uint32_t*>(cpu_addr_)[offset >> 2];
  }

  void write_context_status_gen12(uint32_t offset, std::pair<uint32_t, uint32_t> val) {
    DASSERT((offset & 0x3) == 0);
    DASSERT(offset >= kContextStatusStartOffset && offset <= kContextStatusEndOffsetGen12);
    auto ptr = reinterpret_cast<volatile uint32_t*>(cpu_addr_);
    uint32_t index = offset >> 2;
    ptr[index] = val.first;
    ptr[index + 1] = val.second;
  }

  std::pair<uint32_t, uint32_t> read_context_status_gen12(uint32_t offset) {
    DASSERT((offset & 0x3) == 0);
    DASSERT(offset >= kContextStatusStartOffset && offset <= kContextStatusEndOffsetGen12);
    auto ptr = reinterpret_cast<volatile uint32_t*>(cpu_addr_);
    uint32_t index = offset >> 2;
    return {ptr[index], ptr[index + 1]};
  }

  EngineCommandStreamerId id_;
  std::unique_ptr<GpuMapping> mapping_;
  void* cpu_addr_;
};

#endif  // HARDWARE_STATUS_PAGE_H
