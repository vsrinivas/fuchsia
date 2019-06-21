// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace-test-utils/compare_records.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <trace-reader/reader.h>
#include <trace-reader/reader_internal.h>

#include "trace-test-utils/read_records.h"
#include "trace-test-utils/squelch.h"

namespace trace_testing {

bool CompareRecords(const fbl::Vector<trace::Record>& records,
                    size_t start_record, size_t max_num_records,
                    const char* expected) {
    // Strip out timestamps and other varying data that is not controlled by
    // the tests.
    std::unique_ptr<Squelcher> squelcher =
        Squelcher::Create("([0-9]+/[0-9]+)"
                          "|koid\\(([0-9]+)\\)"
                          "|koid: ([0-9]+)"
                          "|ts: ([0-9]+)"
                          "|end_ts: ([0-9]+)"
                          "|(0x[0-9a-f]+)");
    ZX_DEBUG_ASSERT(squelcher);

    fbl::StringBuffer<16384u> buf;
    size_t num_recs = 0;
    for (size_t i = start_record; i < records.size(); ++i) {
        if (num_recs == max_num_records)
            break;
        const auto& record = records[i];
        fbl::String from_str = record.ToString();
        fbl::String to_str = squelcher->Squelch(from_str.c_str());

        buf.Append(to_str);
        buf.Append('\n');
        ++num_recs;
    }

    if (strcmp(buf.c_str(), expected) != 0) {
        fprintf(stderr, "Records do not match expected contents:\n");
        fprintf(stderr, "Buffer:\n%s\n", buf.c_str());
        fprintf(stderr, "Expected:\n%s\n", expected);
        return false;
    }

    return true;
}

bool ComparePartialBuffer(const fbl::Vector<trace::Record>& records,
                          size_t max_num_records, const char* expected,
                          size_t* out_leading_to_skip) {
    // A valid buffer should at least have the initialization record.
    if (records.size() == 0) {
        fprintf(stderr, "expected an initialization record\n");
        return false;
    }
    if (records[0].type() != trace::RecordType::kInitialization) {
        fprintf(stderr, "expected initialization record\n");
        return false;
    }
    // Sanity check the recorded ticks/seconds.
    if (records[0].GetInitialization().ticks_per_second != zx_ticks_per_second()) {
        fprintf(stderr, "Bad ticks/second field in initialization record\n");
        return false;
    }
    // Done with this record, skip it in further analysis.
    size_t skip_count = 1;

    if (!CompareRecords(records, skip_count, max_num_records, expected)) {
        // Diagnostic messages already printed.
        return false;
    }

    if (out_leading_to_skip) {
        *out_leading_to_skip = skip_count;
    }

    return true;
}

bool CompareBuffer(const fbl::Vector<trace::Record>& records,
                   const char* expected) {
    return ComparePartialBuffer(records, SIZE_MAX, expected, nullptr);
}

} // namespace trace_testing
