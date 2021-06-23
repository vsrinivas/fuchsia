// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains fuzzing targets for sections in the new bytecode.

use bind::bind_program_v2_constants::*;
use bind::match_bind::match_bytecode;
use fuzz::fuzz;
use std::collections::HashMap;

const BIND_HEADER: [u8; 8] = [0x42, 0x49, 0x4E, 0x44, 0x02, 0, 0, 0];

#[fuzz]
fn symbol_table_section_fuzzer(bytes: &[u8]) {
    let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();

    bytecode.extend_from_slice(&SYMB_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&bytes.len().to_be_bytes());
    bytecode.extend_from_slice(bytes);

    bytecode.extend_from_slice(&INSTRUCTION_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&[0, 0, 0, 0]);

    let _ = match_bytecode(bytecode, &HashMap::new());
}

#[fuzz]
fn instruction_section_fuzzer(bytes: &[u8]) {
    let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();

    bytecode.extend_from_slice(&SYMB_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&[0, 0, 0, 0]);

    bytecode.extend_from_slice(&INSTRUCTION_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&bytes.len().to_be_bytes());
    bytecode.extend_from_slice(bytes);

    let _ = match_bytecode(bytecode, &HashMap::new());
}
