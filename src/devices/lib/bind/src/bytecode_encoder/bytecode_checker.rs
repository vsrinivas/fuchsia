// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bytecode_constants::*;

// Constants representing the number of bytes in an operand and value.
pub const OP_BYTES: u32 = 1;
pub const VALUE_BYTES: u32 = 5;
pub const OFFSET_BYTES: u32 = 4;

pub const NODE_HEADER_BYTES: u32 = 9;
pub const COMPOSITE_NAME_ID_BYTES: u32 = 4;

// Constants representing the number of bytes in each instruction.
pub const UNCOND_ABORT_BYTES: u32 = OP_BYTES;
pub const COND_ABORT_BYTES: u32 = OP_BYTES + VALUE_BYTES + VALUE_BYTES;
pub const UNCOND_JMP_BYTES: u32 = OP_BYTES + OFFSET_BYTES;
pub const COND_JMP_BYTES: u32 = OP_BYTES + OFFSET_BYTES + VALUE_BYTES + VALUE_BYTES;
pub const JMP_PAD_BYTES: u32 = OP_BYTES;

pub struct EncodedValue {
    pub value_type: RawValueType,
    pub value: u32,
}

pub struct BytecodeChecker {
    iter: std::vec::IntoIter<u8>,
}

impl BytecodeChecker {
    pub fn new(bytecode: Vec<u8>) -> Self {
        BytecodeChecker { iter: bytecode.into_iter() }
    }

    pub fn verify_next_u8(&mut self, expected: u8) {
        assert_eq!(expected, self.iter.next().unwrap());
    }

    pub fn verify_enable_flag(&mut self, expected_debug_flag: bool) {
        let debug_flag_byte = self.iter.next().unwrap();
        if expected_debug_flag {
            assert_eq!(BYTECODE_ENABLE_DEBUG, debug_flag_byte);
        } else {
            assert_eq!(BYTECODE_DISABLE_DEBUG, debug_flag_byte);
        }
    }

    // Verify the expected value as little-endian and advance the iterator to the next four
    // bytes. This function shouldn't be used for magic numbers, which is in big-endian.
    pub fn verify_next_u32(&mut self, expected: u32) {
        let bytecode = expected.to_le_bytes();
        for i in &bytecode {
            self.verify_next_u8(*i);
        }
    }

    pub fn verify_magic_num(&mut self, expected: u32) {
        let bytecode = expected.to_be_bytes();
        for i in &bytecode {
            self.verify_next_u8(*i);
        }
    }

    pub fn verify_node_header(
        &mut self,
        node_type: RawNodeType,
        node_name: u32,
        num_of_bytes: u32,
    ) {
        self.verify_next_u8(node_type as u8);
        self.verify_next_u32(node_name);
        self.verify_next_u32(num_of_bytes);
    }

    // Verify that the next bytes matches the string and advance
    // the iterator.
    pub fn verify_string(&mut self, expected: String) {
        expected.chars().for_each(|c| self.verify_next_u8(c as u8));
        self.verify_next_u8(0);
    }

    pub fn verify_value(&mut self, val: EncodedValue) {
        self.verify_next_u8(val.value_type as u8);
        self.verify_next_u32(val.value);
    }

    pub fn verify_bind_rules_header(&mut self, expected_debug_flag: bool) {
        self.verify_magic_num(BIND_MAGIC_NUM);
        self.verify_next_u32(BYTECODE_VERSION);
        self.verify_enable_flag(expected_debug_flag);
    }

    pub fn verify_sym_table_header(&mut self, num_of_bytes: u32) {
        self.verify_magic_num(SYMB_MAGIC_NUM);
        self.verify_next_u32(num_of_bytes);
    }

    pub fn verify_instructions_header(&mut self, num_of_bytes: u32) {
        self.verify_magic_num(INSTRUCTION_MAGIC_NUM);
        self.verify_next_u32(num_of_bytes);
    }

    pub fn verify_composite_header(&mut self, num_of_bytes: u32) {
        self.verify_magic_num(COMPOSITE_MAGIC_NUM);
        self.verify_next_u32(num_of_bytes);

        // The device name ID is the first one in the symbol table.
        // This isn't required in the bytecode specs, it's just how it's
        // implemented in encode_composite_to_bytecode().
        self.verify_next_u32(1);
    }

    pub fn verify_debug_header(&mut self, num_of_bytes: u32) {
        self.verify_magic_num(DEBG_MAGIC_NUM);
        self.verify_next_u32(num_of_bytes);
    }

    pub fn verify_symbol_table(&mut self, expected_symbols: &[&str]) {
        let mut unique_id = 1;
        expected_symbols.iter().for_each(|value| {
            self.verify_next_u32(unique_id);
            self.verify_string(value.to_string());
            unique_id += 1;
        });
    }

    pub fn verify_unconditional_abort(&mut self) {
        self.verify_next_u8(0x30);
    }

    pub fn verify_abort_not_equal(&mut self, lhs: EncodedValue, rhs: EncodedValue) {
        self.verify_next_u8(0x01);
        self.verify_value(lhs);
        self.verify_value(rhs);
    }

    pub fn verify_abort_equal(&mut self, lhs: EncodedValue, rhs: EncodedValue) {
        self.verify_next_u8(0x02);
        self.verify_value(lhs);
        self.verify_value(rhs);
    }

    pub fn verify_unconditional_jmp(&mut self, offset: u32) {
        self.verify_next_u8(0x10);
        self.verify_next_u32(offset);
    }

    pub fn verify_jmp_if_equal(&mut self, offset: u32, lhs: EncodedValue, rhs: EncodedValue) {
        self.verify_next_u8(0x11);
        self.verify_next_u32(offset);
        self.verify_value(lhs);
        self.verify_value(rhs);
    }

    pub fn verify_jmp_if_not_equal(&mut self, offset: u32, lhs: EncodedValue, rhs: EncodedValue) {
        self.verify_next_u8(0x12);
        self.verify_next_u32(offset);
        self.verify_value(lhs);
        self.verify_value(rhs);
    }

    pub fn verify_jmp_pad(&mut self) {
        self.verify_next_u8(0x20);
    }

    // Verify that the iterator reached the end of the bytecode.
    pub fn verify_end(&mut self) {
        assert_eq!(None, self.iter.next());
    }
}
