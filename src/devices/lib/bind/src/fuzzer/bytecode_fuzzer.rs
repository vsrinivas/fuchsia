// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains fuzzing targets for the new bytecode.

use bind::interpreter::match_bind::match_bytecode;
use fuzz::fuzz;
use std::collections::HashMap;

#[fuzz]
fn bind_rules_bytecode_fuzzer(bytes: &[u8]) {
    let _ = match_bytecode(bytes.to_vec(), &HashMap::new());
}
