// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Helper functions for setting up and tearing down a test fixture which
// manages the trace engine on behalf of a test.
//

#pragma once

#include <magenta/compiler.h>
#include <unittest/unittest.h>

__BEGIN_CDECLS

void fixture_set_up(void);
void fixture_tear_down(void);
void fixture_start_tracing(void);
void fixture_stop_tracing(void);
void fixture_stop_tracing_hard(void);
mx_status_t fixture_get_disposition(void);
bool fixture_compare_records(const char* expected);

inline void fixture_scope_cleanup(bool* scope) {
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
