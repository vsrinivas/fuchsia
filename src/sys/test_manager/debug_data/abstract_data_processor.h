// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_ABSTRACT_DATA_PROCESSOR_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_ABSTRACT_DATA_PROCESSOR_H_

#include <string>

#include "common.h"

class AbstractDataProcessor {
 public:
  virtual ~AbstractDataProcessor() = default;
  virtual void ProcessData(std::string test_url, DataSinkDump data_sink) = 0;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_ABSTRACT_DATA_PROCESSOR_H_
