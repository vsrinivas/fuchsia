// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains fuzzing targets for the diagnostic streams crate.

use diagnostics_stream::parse;
use fuzz::fuzz;

#[fuzz]
fn parse_record_fuzzer(bytes: &[u8]) {
    // ignore errors here, for the purpose of fuzzing we only care about panics.
    let _ = parse::parse_record(bytes);
}
