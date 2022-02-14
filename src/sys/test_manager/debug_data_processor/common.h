// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_COMMON_H_
#define SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_COMMON_H_

#include <lib/zx/vmo.h>

#include <string>

struct DataSinkDump {
  std::string data_sink;
  zx::vmo vmo;
};

#endif  // SRC_SYS_TEST_MANAGER_DEBUG_DATA_PROCESSOR_COMMON_H_
