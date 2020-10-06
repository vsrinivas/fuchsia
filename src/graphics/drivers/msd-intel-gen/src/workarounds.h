// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WORKAROUNDS_H
#define WORKAROUNDS_H

#include <stdint.h>

#include <magma_util/instruction_writer.h>

#include "types.h"

class Workarounds {
 public:
  // Returns the number of bytes required to write into the instruction stream.
  static uint32_t InstructionBytesRequired();

  // Assumes there is sufficient space available to write into the instruction
  // stream. Caller should check |InstructionBytesRequired| first.
  static bool Init(magma::InstructionWriter* writer, EngineCommandStreamerId engine_id);
};

#endif  // WORKAROUNDS_H
