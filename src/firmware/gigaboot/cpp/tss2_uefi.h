// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_TSS2_UEFI_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_TSS2_UEFI_H_

#include <fbl/vector.h>

#include "tss2/tss2_sys.h"
#include "tss2/tss2_tcti.h"

namespace gigaboot {

// Randomly generated mgaic
constexpr uint64_t kTctiUefiMagic = 0xEA8F867A73793DCD;
constexpr uint32_t kTctiUefiVersion = 1;

struct Tss2UefiTctiContext {
  // The first field must be a TSS2_TCTI_CONTEXT_COMMON_V2. TSS code will cast the pointer of
  // this structure into a TSS2_TCTI_CONTEXT* for accessing callbacks. (it works because
  // TSS2_TCTI_CONTEXT is also the first field in TSS2_TCTI_CONTEXT_COMMON_V2).
  TSS2_TCTI_CONTEXT_COMMON_V2 common;

  // Other fields are implementation specific data.
  fbl::Vector<uint8_t> command_buffer;
  size_t current_command_size = 0;
};

// A wrapper for TSS2_SYS_CONTEXT that manages and owns the buffers backing the TSS2_SYS_CONTEXT
// structure and the associated TCTI context data structure.
class Tss2UefiSysContext {
 public:
  Tss2UefiSysContext() {}

  static std::unique_ptr<Tss2UefiSysContext> Create();

  TSS2_SYS_CONTEXT* sys_context() {
    return reinterpret_cast<TSS2_SYS_CONTEXT*>(sys_context_.data());
  }

  Tss2UefiTctiContext* tcti_context() { return tcti_context_.get(); }

 private:
  // TSS2_SYS_CONTEXT is a variable length struct. Thus we use a vector
  // as the backing buffer.
  fbl::Vector<uint8_t> sys_context_;

  // The TCTI context associated with the TSS2_SYS_CONTEXT
  std::unique_ptr<Tss2UefiTctiContext> tcti_context_;
};

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_TSS2_UEFI_H_
