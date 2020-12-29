// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_trace as trace;

#[no_mangle]
pub extern "C" fn rs_test_trace_enabled() -> bool {
    return trace::is_enabled();
}

#[no_mangle]
pub extern "C" fn rs_test_category_disabled() -> bool {
    return trace::category_enabled(trace::cstr!("-disabled"));
}

#[no_mangle]
pub extern "C" fn rs_test_category_enabled() -> bool {
    return trace::category_enabled(trace::cstr!("+enabled"));
}

#[no_mangle]
pub extern "C" fn rs_test_counter_macro() {
    trace::counter!("+enabled", "name", 42, "arg" => 10);
}

#[no_mangle]
pub extern "C" fn rs_test_instant_macro() {
    trace::instant!("+enabled", "name", trace::Scope::Process, "arg" => 10);
}

#[no_mangle]
pub extern "C" fn rs_test_duration_macro() {
    trace::duration!("+enabled", "name", "x" => 5, "y" => 10);
}

#[no_mangle]
pub extern "C" fn rs_test_duration_macro_with_scope() {
    // N.B. The ordering here is intentional. The duration! macro emits a trace
    // event when the scoped object is dropped. From an output perspective,
    // that means we are looking to see that the instant event occurs first.
    trace::duration!("+enabled", "name", "x" => 5, "y" => 10);
    trace::instant!("+enabled", "name", trace::Scope::Process, "arg" => 10);
}

#[no_mangle]
pub extern "C" fn rs_test_duration_begin_end_macros() {
    trace::duration_begin!("+enabled", "name", "x" => 5);
    trace::instant!("+enabled", "name", trace::Scope::Process, "arg" => 10);
    trace::duration_end!("+enabled", "name", "y" => "foo");
}

#[no_mangle]
pub extern "C" fn rs_test_blob_macro() {
    trace::blob!("+enabled", "name", "blob contents".as_bytes().to_vec().as_slice(), "x" => 5);
}

#[no_mangle]
pub extern "C" fn rs_test_flow_begin_step_end_macros() {
    trace::flow_begin!("+enabled", "name", 123, "x" => 5);
    trace::flow_step!("+enabled", "step", 123, "z" => 42);
    trace::flow_end!("+enabled", "name", 123, "y" => "foo");
}

#[no_mangle]
pub extern "C" fn rs_test_arglimit() {
    trace::duration!("+enabled", "name",
        "1" => 1,
        "2" => 2,
        "3" => 3,
        "4" => 4,
        "5" => 5,
        "6" => 6,
        "7" => 7,
        "8" => 8,
        "9" => 9,
        "10" => 10,
        "11" => 11,
        "12" => 12,
        "13" => 13,
        "14" => 14,
        "15" => 15);
}
