// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler::BindProgram;

/// Functions for encoding the new bytecode format. When the
/// old bytecode format is deleted, the "v2" should be removed from the names.

pub fn encode_to_bytecode_v2(_bind_program: BindProgram) -> Vec<u8> {
    // Unimplemented, new bytecode is not implemented yet. See fxb/67440.
    vec![]
}

pub fn encode_to_string_v2(_bind_program: BindProgram) -> String {
    // Unimplemented, new bytecode is not implemented yet. See fxb/67440.
    "".to_string()
}
