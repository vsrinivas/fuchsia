// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_TRANSCEIVER_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_TRANSCEIVER_H_

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/transceiver.h"

namespace fuzzing {

// Wraps a |Transceiver| and provides for synchronous transmission and receipt of data.
class FakeTransceiver final {
 public:
  FakeTransceiver() = default;
  ~FakeTransceiver() = default;

  // Synchronously send an |Input|. The input can be read from the returned |FidlInput|.
  FidlInput Transmit(Input input) FXL_LOCKS_EXCLUDED(mutex_);

  // Synchronously receives and returns an |Input| from a provided |FidlInput|.
  Input Receive(FidlInput fidl_input) FXL_LOCKS_EXCLUDED(mutex_);

 private:
  Transceiver transceiver_ FXL_GUARDED_BY(mutex_);
  std::mutex mutex_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeTransceiver);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_TRANSCEIVER_H_
