// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_ABSTRACT_DATA_PROCESSOR_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_ABSTRACT_DATA_PROCESSOR_H_

#include <lib/zx/event.h>

#include <string>

#include "common.h"

const uint32_t DATA_FLUSHED_SIGNAL = ZX_USER_SIGNAL_0;

class AbstractDataProcessor {
 public:
  virtual ~AbstractDataProcessor() = default;
  virtual void ProcessData(std::string test_url, DataSinkDump data_sink) = 0;
  /// Indicates that no more data will be added via ProcessData. After this call,
  /// ProcessData or FinishProcessing may not be called again.
  virtual void FinishProcessing() = 0;
  /// Returns a handle to an event on which `DATA_FLUSHED_SIGNAL` is signalled when
  /// all data passed through ProcessData has been processed.
  virtual zx::unowned_event GetDataFlushedEvent() = 0;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_ABSTRACT_DATA_PROCESSOR_H_
