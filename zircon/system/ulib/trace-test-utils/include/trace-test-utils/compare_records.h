// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

// TODO(dje): Conversion to std::vector.
#include <fbl/vector.h>
#include <trace-reader/reader.h>

#include <utility>

namespace trace_testing {

// Compare the pretty-printed form of |records| with |expected|.
// Comparison begins |start_records| in and continues for |max_num_records|.
// Returns true on success.
bool CompareRecords(const fbl::Vector<trace::Record>& records, size_t start_record,
                    size_t max_num_records, const char* expected);

// Compare the pretty-printed form of |records| with |expected|.
// |records| is assumed to have been created with ReadRecords().
// Comparison proceeds for at most |max_num_records| records.
// If non-null, |*out_leading_to_skip| contains the number of leading
// records in |records| to skip in further analysis. Typically these are
// administrative records emitted at the start by the tracing system.
// |max_num_records| does *not* include these leading administrative records.
// Returns true on success.
bool ComparePartialBuffer(const fbl::Vector<trace::Record>& records, size_t max_num_records,
                          const char* expected, size_t* out_leading_to_skip);

// Compare the pretty-printed form of |records| with |expected|.
// |records| is assumed to have been created with ReadRecords().
// Returns true on success.
bool CompareBuffer(const fbl::Vector<trace::Record>& records, const char* expected);

}  // namespace trace_testing
