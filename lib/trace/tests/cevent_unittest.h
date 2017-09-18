// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <stdbool.h>

__BEGIN_CDECLS

bool cevent_test_enabled(void);

bool cevent_test_category_enabled(void);

bool cevent_test_trace_nonce(void);

bool cevent_test_instant(void);

bool cevent_test_counter(void);

bool cevent_test_duration(void);

bool cevent_test_duration_begin(void);

bool cevent_test_duration_end(void);

bool cevent_test_async_begin(void);

bool cevent_test_async_instant(void);

bool cevent_test_async_end(void);

bool cevent_test_flow_begin(void);

bool cevent_test_flow_step(void);

bool cevent_test_flow_end(void);

bool cevent_test_handle(void);

bool cevent_test_null_arguments(void);

bool cevent_test_integral_arguments(void);

bool cevent_test_enum_arguments(void);

bool cevent_test_float_arguments(void);

bool cevent_test_string_arguments(void);

bool cevent_test_pointer_arguments(void);

bool cevent_test_koid_arguments(void);

// Utilities needed by the tests.
void ctrace_stop_tracing(void);

__END_CDECLS
