// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_INSTRUCTION_WRITER_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_INSTRUCTION_WRITER_H_

namespace magma {

// Interface for writing instructions 32-bits at a time
class InstructionWriter {
 public:
  virtual ~InstructionWriter() = default;

  virtual void Write32(uint32_t value) = 0;
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_INSTRUCTION_WRITER_H_
