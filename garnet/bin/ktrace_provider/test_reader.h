// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_KTRACE_PROVIDER_TEST_READER_H_
#define GARNET_BIN_KTRACE_PROVIDER_TEST_READER_H_

#include <src/lib/fxl/macros.h>

#include "garnet/bin/ktrace_provider/reader.h"

namespace ktrace_provider {

class TestReader : public Reader {
 public:
  TestReader(const void* trace_data, size_t trace_data_size);

 private:
  void ReadMoreData() override;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestReader);
};

}  // namespace ktrace_provider

#endif  // GARNET_BIN_KTRACE_PROVIDER_TEST_READER_H_
