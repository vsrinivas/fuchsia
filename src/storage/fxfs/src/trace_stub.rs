// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stubbed version of trace.rs for non-fuchsia builds.

#[macro_export]
macro_rules! trace_duration {
    ($name:expr $(, $key:expr => $val:expr)*) => {};
}

#[macro_export]
macro_rules! trace_instant {
    ($name:expr, $scope:expr $(, $key:expr => $val:expr)*) => {};
}

#[macro_export]
macro_rules! trace_flow_begin {
    ($name:expr, $flow_id:expr $(, $key:expr => $val:expr)*) => {};
}

#[macro_export]
macro_rules! trace_flow_step {
    ($name:expr, $flow_id:expr $(, $key:expr => $val:expr)*) => {};
}

#[macro_export]
macro_rules! trace_flow_end {
    ($name:expr, $flow_id:expr $(, $key:expr => $val:expr)*) => {};
}
