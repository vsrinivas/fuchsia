// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/test_reader.h"

namespace ktrace_provider {

TestReader::TestReader(const void* trace_data, size_t trace_data_size)
  : Reader(reinterpret_cast<const char*>(trace_data), trace_data_size) {
  // Mark the end of "read" data.
  marker_ = end_;
}

void TestReader::ReadMoreData() {
  // There is no more.
  current_ = marker_;
}

}  // namespace ktrace_provider
