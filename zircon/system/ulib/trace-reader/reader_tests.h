// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <trace-reader/reader.h>

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

namespace trace {
namespace test {

template <typename T>
uint64_t ToWord(const T& value) {
  return *reinterpret_cast<const uint64_t*>(&value);
}

static inline trace::TraceReader::RecordConsumer MakeRecordConsumer(
    fbl::Vector<trace::Record>* out_records) {
  return [out_records](trace::Record record) { out_records->push_back(std::move(record)); };
}

static inline trace::TraceReader::ErrorHandler MakeErrorHandler(fbl::String* out_error) {
  return [out_error](fbl::String error) { *out_error = std::move(error); };
}

}  // namespace test
}  // namespace trace
