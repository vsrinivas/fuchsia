// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace-test-utils/read_records.h"

#include <stdio.h>

#include <utility>

#include <trace-reader/reader_internal.h>

namespace trace_testing {

bool ReadRecordsAndErrors(const uint8_t* buffer, size_t buffer_size,
                          fbl::Vector<trace::Record>* out_records,
                          fbl::Vector<fbl::String>* out_errors) {
    trace::TraceReader reader(
        [out_records](trace::Record record) {
            out_records->push_back(std::move(record));
        },
        [out_errors](fbl::String error) {
            out_errors->push_back(std::move(error));
        });

    trace::internal::TraceBufferReader buffer_reader(
        [&reader](trace::Chunk chunk) {
            if (!reader.ReadRecords(chunk)) {
                // Nothing to do, error already recorded.
            }
        },
        [out_errors](fbl::String error) {
            out_errors->push_back(std::move(error));
        });

    return buffer_reader.ReadChunks(buffer, buffer_size);
}

bool ReadRecords(const uint8_t* buffer, size_t buffer_size,
                 fbl::Vector<trace::Record>* out_records) {
    fbl::Vector<fbl::String> errors;
    if (!ReadRecordsAndErrors(buffer, buffer_size, out_records, &errors)) {
        fprintf(stderr, "Error reading buffer\n");
        return false;
    }

    for (const auto& error : errors) {
        fprintf(stderr, "error: %s\n", error.c_str());
    }
    if (errors.size() != 0) {
        fprintf(stderr, "Errors encountered\n");
        return false;
    }

    return true;
}

}  // namespace trace_testing
