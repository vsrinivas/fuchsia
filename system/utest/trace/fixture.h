// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Helper functions for setting up and tearing down a test fixture which
// manages the trace engine on behalf of a test.
//

#pragma once

#include <stddef.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>

#ifdef __cplusplus
#include <fbl/string.h>
#include <fbl/vector.h>
#include <trace-reader/records.h>
#endif

#ifdef __cplusplus

// FixtureSquelch is used to filter out elements of a trace record that may
// vary run to run or even within a run and are not germaine to determining
// correctness. The canonical example is record timestamps.
// The term "squelch" derives from radio circuitry used to remove noise.
struct FixtureSquelch;

// |regex_str| is a regular expression consistenting of one or more
// subexpressions, the text in the parenthesis of each matching expressions
// is replaced with '<>'.
// Best illustration is an example. This example removes decimal numbers,
// koids, timestamps ("ts"), and lowercase hex numbers.
// const char regex[] = "([0-9]+/[0-9]+)"
//   "|koid\\(([0-9]+)\\)"
//   "|koid: ([0-9]+)"
//   "|ts: ([0-9]+)"
//   "|(0x[0-9a-f]+)";
// So "ts: 123 42 mumble koid(456) foo koid: 789, bar 0xabcd"
// becomes "ts: <> <> mumble koid(<>) foo koid: <>, bar <>".
bool fixture_create_squelch(const char* regex_str, FixtureSquelch** out_squelch);
void fixture_destroy_squelch(FixtureSquelch* squelch);
fbl::String fixture_squelch(FixtureSquelch* squelch, const char* str);

bool fixture_compare_raw_records(const fbl::Vector<trace::Record>& records,
                                 size_t start_record, size_t max_num_records,
                                 const char* expected);
bool fixture_compare_n_records(size_t max_num_records, const char* expected,
                               fbl::Vector<trace::Record>* records);

#endif

__BEGIN_CDECLS

void fixture_set_up(void);
void fixture_tear_down(void);
void fixture_start_tracing(void);
void fixture_stop_tracing(void);
void fixture_stop_tracing_hard(void);
zx_status_t fixture_get_disposition(void);
bool fixture_compare_records(const char* expected);

static inline void fixture_scope_cleanup(bool* scope) {
    fixture_tear_down();
}

#define BEGIN_TRACE_TEST                                          \
    BEGIN_TEST;                                                   \
    __attribute__((cleanup(fixture_scope_cleanup))) bool __scope; \
    (void)__scope;                                                \
    fixture_set_up();

#define END_TRACE_TEST \
    END_TEST;

#ifndef NTRACE
#ifdef __cplusplus
#define ASSERT_RECORDS(expected_c, expected_cpp) \
    ASSERT_TRUE(fixture_compare_records(expected_c expected_cpp), "record mismatch");
#else
#define ASSERT_RECORDS(expected_c, expected_cpp) \
    ASSERT_TRUE(fixture_compare_records(expected_c), "record mismatch");
#endif // __cplusplus
#else
#define ASSERT_RECORDS(expected_c, expected_cpp) \
    ASSERT_TRUE(fixture_compare_records(""), "record mismatch");
#endif // NTRACE

__END_CDECLS
