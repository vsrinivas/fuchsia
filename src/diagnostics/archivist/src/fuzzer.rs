// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains fuzzing targets for Archivist.

use archivist_lib::{internal_logging, logs};
use fuzz::fuzz;

/// Fuzzer for kernel debuglog parser.
#[fuzz]
fn convert_debuglog_to_log_message_fuzzer(bytes: &[u8]) {
    internal_logging::init("fuzzer");
    logs::convert_debuglog_to_log_message(bytes);
}
