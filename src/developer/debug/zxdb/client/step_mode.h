// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

// Several thread controllers manage different types of stepping. This enum
// defines the possibilities.
enum class StepMode {
  kAddressRange,  // Steps in an address range.
  kSourceLine,    // Steps in the current source line.
  kInstruction    // Steps for the current CPU instruction.
};

}  // namespace zxdb
