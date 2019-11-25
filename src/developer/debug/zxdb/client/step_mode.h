// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_MODE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_MODE_H_

namespace zxdb {

// Several thread controllers manage different types of stepping. This enum defines the
// possibilities.
enum class StepMode {
  kAddressRange,  // Steps in an address range.
  kSourceLine,    // Steps in the current source line.
  kInstruction    // Steps for the current CPU instruction.
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STEP_MODE_H_
