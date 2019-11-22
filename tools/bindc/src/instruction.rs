// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bind program instructions

#![allow(dead_code)]

use bitfield::{bitfield, BitRange};
use std::io::{self, Write};

bitfield! {
    /// Each instruction is a pair of 32 bit unsigned integers divided as
    /// follows.
    /// lsb           msb
    /// COAABBBB VVVVVVVV  Condition Opcode paramA paramB Value
    struct RawInstruction([u32]);
    u32;
    condition, set_condition: 3, 0;
    operation, set_operation: 7, 4;
    parameter_a, set_parameter_a: 15, 8;
    parameter_b, set_parameter_b: 31, 16;
    value, set_value: 63, 32;
}

/// For all conditions (except Always), the operands to the condition
/// are (parameter_b, value) pairs in the final encoding.
#[derive(PartialEq, Eq)]
pub enum Condition {
    Always,
    Equal(u32, u32),
    NotEqual(u32, u32),
    GreaterThan(u32, u32),
    LessThan(u32, u32),
    GreaterThanEqual(u32, u32),
    LessThanEqual(u32, u32),
}

impl Condition {
    fn to_raw(self) -> (u32, u32, u32) {
        match self {
            Condition::Always => (0, 0, 0),
            Condition::Equal(b, v) => (1, b, v),
            Condition::NotEqual(b, v) => (2, b, v),
            Condition::GreaterThan(b, v) => (3, b, v),
            Condition::LessThan(b, v) => (4, b, v),
            Condition::GreaterThanEqual(b, v) => (5, b, v),
            Condition::LessThanEqual(b, v) => (6, b, v),
        }
    }
}

pub enum Instruction {
    Abort(Condition),
    Match(Condition),
    Goto(Condition, u32),
    Label(u32),
}

impl Instruction {
    pub fn encode(self, output: &mut dyn Write) -> Result<(), io::Error> {
        let raw = self.to_raw();
        let bit_range: u64 = raw.bit_range(63, 0);
        output.write(&bit_range.to_le_bytes())?;
        Ok(())
    }

    pub fn encode_pair(self) -> (u32, u32) {
        let RawInstruction([word0, word1]) = self.to_raw();
        (word0, word1)
    }

    fn to_raw(self) -> RawInstruction<[u32; 2]> {
        let (c, o, a, b, v) = match self {
            Instruction::Abort(condition) => {
                let (c, b, v) = condition.to_raw();
                (c, 0, 0, b, v)
            }
            Instruction::Match(condition) => {
                let (c, b, v) = condition.to_raw();
                (c, 1, 0, b, v)
            }
            Instruction::Goto(condition, a) => {
                let (c, b, v) = condition.to_raw();
                (c, 2, a, b, v)
            }
            Instruction::Label(a) => (0, 5, a, 0, 0),
        };
        let mut raw_instruction = RawInstruction([0, 0]);
        raw_instruction.set_condition(c);
        raw_instruction.set_operation(o);
        raw_instruction.set_parameter_a(a);
        raw_instruction.set_parameter_b(b);
        raw_instruction.set_value(v);
        raw_instruction
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_raw_instruction() {
        let mut raw = RawInstruction([0, 0]);
        assert_eq!(raw.0[0], 0);
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 0);
        assert_eq!(raw.operation(), 0);
        assert_eq!(raw.parameter_a(), 0);
        assert_eq!(raw.parameter_b(), 0);
        assert_eq!(raw.value(), 0);

        raw.set_condition(1);
        assert_eq!(raw.0[0], 1);
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 0);
        assert_eq!(raw.parameter_a(), 0);
        assert_eq!(raw.parameter_b(), 0);
        assert_eq!(raw.value(), 0);

        raw.set_operation(2);
        assert_eq!(raw.0[0], 1 | (2 << 4));
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 2);
        assert_eq!(raw.parameter_a(), 0);
        assert_eq!(raw.parameter_b(), 0);
        assert_eq!(raw.value(), 0);

        raw.set_parameter_a(3);
        assert_eq!(raw.0[0], 1 | (2 << 4) | (3 << 8));
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 2);
        assert_eq!(raw.parameter_a(), 3);
        assert_eq!(raw.parameter_b(), 0);
        assert_eq!(raw.value(), 0);

        raw.set_parameter_b(4);
        assert_eq!(raw.0[0], 1 | (2 << 4) | (3 << 8) | (4 << 16));
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 2);
        assert_eq!(raw.parameter_a(), 3);
        assert_eq!(raw.parameter_b(), 4);
        assert_eq!(raw.value(), 0);

        raw.set_value(5);
        assert_eq!(raw.0[0], 1 | (2 << 4) | (3 << 8) | (4 << 16));
        assert_eq!(raw.0[1], 5);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 2);
        assert_eq!(raw.parameter_a(), 3);
        assert_eq!(raw.parameter_b(), 4);
        assert_eq!(raw.value(), 5)
    }

    #[test]
    fn test_abort_value() {
        let instruction = Instruction::Abort(Condition::Always);
        let raw_instruction = instruction.to_raw();
        assert_eq!(raw_instruction.0[0], 0 << 4);
        assert_eq!(raw_instruction.0[1], 0);
        assert_eq!(raw_instruction.operation(), 0)
    }

    #[test]
    fn test_match_value() {
        let instruction = Instruction::Match(Condition::Always);
        let raw_instruction = instruction.to_raw();
        assert_eq!(raw_instruction.0[0], 1 << 4);
        assert_eq!(raw_instruction.0[1], 0);
        assert_eq!(raw_instruction.operation(), 1)
    }

    #[test]
    fn test_goto_value() {
        let instruction = Instruction::Goto(Condition::Always, 0);
        let raw_instruction = instruction.to_raw();
        assert_eq!(raw_instruction.0[0], 2 << 4);
        assert_eq!(raw_instruction.0[1], 0);
        assert_eq!(raw_instruction.operation(), 2)
    }

    #[test]
    fn test_label_value() {
        let instruction = Instruction::Label(0);
        let raw_instruction = instruction.to_raw();
        assert_eq!(raw_instruction.0[0], 5 << 4);
        assert_eq!(raw_instruction.0[1], 0);
        assert_eq!(raw_instruction.operation(), 5)
    }

    #[test]
    fn test_complicated_value() {
        let instruction = Instruction::Goto(Condition::LessThan(23, 1234), 42);
        let raw_instruction = instruction.to_raw();
        assert_eq!(raw_instruction.0[0], 4 | (2 << 4) | (42 << 8) | (23 << 16));
        assert_eq!(raw_instruction.0[1], 1234);
        assert_eq!(raw_instruction.condition(), 4);
        assert_eq!(raw_instruction.operation(), 2);
        assert_eq!(raw_instruction.parameter_a(), 42);
        assert_eq!(raw_instruction.parameter_b(), 23);
        assert_eq!(raw_instruction.value(), 1234)
    }

    #[test]
    fn test_encode() {
        let instruction = Instruction::Goto(Condition::LessThan(23, 1234), 42);
        let mut buf: Vec<u8> = vec![];
        instruction.encode(&mut buf).unwrap();
        assert_eq!(buf[0], 4 | (2 << 4));
        assert_eq!(buf[1], 42);
        assert_eq!(buf[2], 23);
        assert_eq!(buf[3], 0);
        assert_eq!(buf[4], 210);
        assert_eq!(buf[5], 4);
        assert_eq!(buf[6], 0);
        assert_eq!(buf[7], 0);
    }
}
