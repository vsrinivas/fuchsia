// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

// TODO(dje): Conversion to std::string needs to be done in conjunction with
// converting trace-reader.
#include <fbl/string.h>
// TODO(dje): Similarly, conversion to std::vector.
#include <fbl/vector.h>

#include <trace-reader/reader.h>

namespace trace_testing {

// Return records from |buffer|.
// Errors detected while reading are returned in |*out_errors| and do not
// cause failure.
// Returns true on success.
bool ReadRecordsAndErrors(const uint8_t* buffer, size_t buffer_size,
                          fbl::Vector<trace::Record>* out_records,
                          fbl::Vector<fbl::String>* out_errors);

// Return records from |buffer|.
// Returns true on success.
bool ReadRecords(const uint8_t* buffer, size_t buffer_size,
                 fbl::Vector<trace::Record>* out_records);

}  // namespace trace_testing
