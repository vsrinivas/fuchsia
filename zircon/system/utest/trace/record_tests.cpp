// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fixture.h"

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>
#include <trace/event.h>
#include <trace-engine/instrumentation.h>
#include <zircon/syscalls.h>

namespace {

static bool blob_test(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    const char name[] = "name";
    trace_string_ref_t name_ref = trace_make_inline_c_string_ref(name);
    const char blob[] = "abc";

    {
        auto context = trace::TraceContext::Acquire();

        trace_context_write_blob_record(context.get(),
                                        TRACE_BLOB_TYPE_DATA,
                                        &name_ref,
                                        blob, sizeof(blob));
    }

    auto expected = fbl::StringPrintf("Blob(name: %s, size: %zu)\n",
                                      name, sizeof(blob));
    EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

    END_TRACE_TEST;
}

static bool blob_macro_test(void) {
    BEGIN_TRACE_TEST;

    fixture_start_tracing();

    const char name[] = "all-byte-values";
    char blob[256];
    for (unsigned i = 0; i < sizeof(blob); ++i) {
        blob[i] = static_cast<char>(i);
    }

    TRACE_BLOB(TRACE_BLOB_TYPE_DATA, name, blob, sizeof(blob));
    auto expected = fbl::StringPrintf("String(index: 1, \"%s\")\n"
                                      "Blob(name: %s, size: %zu)\n",
                                      name, name, sizeof(blob));
    EXPECT_TRUE(fixture_compare_records(expected.c_str()), "record mismatch");

    END_TRACE_TEST;
}

} // namespace

BEGIN_TEST_CASE(records)
RUN_TEST(blob_test)
RUN_TEST(blob_macro_test)
END_TEST_CASE(records)
