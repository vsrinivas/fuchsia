// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bind program instructions

use bitfield::bitfield;
use fidl_fuchsia_device_manager;
use num_derive::FromPrimitive;
use std::fmt;

pub struct DeviceProperty {
    pub key: u32,
    pub value: u32,
}

impl fmt::Display for DeviceProperty {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:#06x} = {:#010x}", self.key, self.value)
    }
}

impl From<fidl_fuchsia_device_manager::DeviceProperty> for DeviceProperty {
    fn from(property: fidl_fuchsia_device_manager::DeviceProperty) -> Self {
        DeviceProperty { key: property.id as u32, value: property.value }
    }
}

bitfield! {
    /// Each instruction is three 32 bit unsigned integers divided as follows.
    /// lsb                    msb
    /// COAABBBB VVVVVVVV DDDDDDDD Condition Opcode paramA paramB Value Debug
    ///
    /// The Debug field contains the following information:
    ///  - line: The source code line which the instruction was compiled from.
    ///  - ast_location: Where in the AST the instruction was compiled from, encoded by the
    ///      RawAstLocation enum.
    ///  - extra: Additional debugging information, meaning depends on the value of ast_location.
    ///      If ast_location is AcceptStatementFailure, extra is the key of the accept statement.
    ///      Otherwise, extra is unused.
    pub struct RawInstruction([u32]);
    u32;
    pub condition, set_condition: 31, 28;
    pub operation, set_operation: 27, 24;
    pub parameter_a, set_parameter_a: 23, 16;
    pub parameter_b, set_parameter_b: 15, 0;
    pub value, set_value: 63, 32;
    pub line, set_line: 95, 88;
    pub ast_location, set_ast_location: 87, 80;
    pub extra, set_extra: 79, 64;
}

impl fmt::Display for RawInstruction<[u32; 3]> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "c: {}, o: {}, a: {}, b: {:#06x}, v: {:#010x}",
            self.condition(),
            self.operation(),
            self.parameter_a(),
            self.parameter_b(),
            self.value()
        )
    }
}

/// These should match the values in <ddk/binding.h>, e.g. COND_AL = 0
#[derive(FromPrimitive, PartialEq, Clone, Copy, Debug)]
pub enum RawCondition {
    Always = 0,
    Equal,
    NotEqual,
}

/// These should match the values in <ddk/binding.h>, e.g. OP_ABORT = 0
#[derive(FromPrimitive, PartialEq, Clone, Copy)]
pub enum RawOp {
    Abort = 0,
    Match,
    Goto,
    Label = 5,
}

/// For all conditions (except Always), the operands to the condition
/// are (parameter_b, value) pairs in the final encoding.
#[derive(PartialEq, Eq)]
pub enum Condition {
    Always,
    Equal(u32, u32),
    NotEqual(u32, u32),
}

impl Condition {
    fn to_raw(self) -> (u32, u32, u32) {
        match self {
            Condition::Always => (RawCondition::Always as u32, 0, 0),
            Condition::Equal(b, v) => (RawCondition::Equal as u32, b, v),
            Condition::NotEqual(b, v) => (RawCondition::NotEqual as u32, b, v),
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
    pub fn to_raw(self) -> RawInstruction<[u32; 3]> {
        let (c, o, a, b, v) = match self {
            Instruction::Abort(condition) => {
                let (c, b, v) = condition.to_raw();
                (c, RawOp::Abort as u32, 0, b, v)
            }
            Instruction::Match(condition) => {
                let (c, b, v) = condition.to_raw();
                (c, RawOp::Match as u32, 0, b, v)
            }
            Instruction::Goto(condition, a) => {
                let (c, b, v) = condition.to_raw();
                (c, RawOp::Goto as u32, a, b, v)
            }
            Instruction::Label(a) => (RawCondition::Always as u32, RawOp::Label as u32, a, 0, 0),
        };
        let mut raw_instruction = RawInstruction([0, 0, 0]);
        raw_instruction.set_condition(c);
        raw_instruction.set_operation(o);
        raw_instruction.set_parameter_a(a);
        raw_instruction.set_parameter_b(b);
        raw_instruction.set_value(v);
        raw_instruction
    }
}

#[derive(FromPrimitive, PartialEq)]
pub enum RawAstLocation {
    Invalid = 0,
    ConditionStatement,
    AcceptStatementValue,
    AcceptStatementFailure,
    IfCondition,
    AbortStatement,
}

pub struct InstructionDebugInfo {
    pub line: u32,
    pub ast_location: RawAstLocation,
    pub extra: u32,
}

impl InstructionDebugInfo {
    pub fn none() -> Self {
        InstructionDebugInfo { line: 0, ast_location: RawAstLocation::Invalid, extra: 0 }
    }

    fn to_raw(self) -> (u32, u32, u32) {
        (self.line, self.ast_location as u32, self.extra)
    }
}

pub struct InstructionDebug {
    pub instruction: Instruction,
    pub debug: InstructionDebugInfo,
}

impl InstructionDebug {
    pub fn new(instruction: Instruction) -> Self {
        InstructionDebug { instruction, debug: InstructionDebugInfo::none() }
    }

    pub fn encode(self) -> (u32, u32, u32) {
        let RawInstruction([word0, word1, word2]) = self.to_raw();
        (word0, word1, word2)
    }

    pub fn to_raw(self) -> RawInstruction<[u32; 3]> {
        let mut raw_instruction = self.instruction.to_raw();

        let (line, ast_location, extra) = self.debug.to_raw();
        raw_instruction.set_line(line);
        raw_instruction.set_ast_location(ast_location);
        raw_instruction.set_extra(extra);

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
        assert_eq!(raw.0[0], 1 << 28);
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 0);
        assert_eq!(raw.parameter_a(), 0);
        assert_eq!(raw.parameter_b(), 0);
        assert_eq!(raw.value(), 0);

        raw.set_operation(2);
        assert_eq!(raw.0[0], (1 << 28) | (2 << 24));
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 2);
        assert_eq!(raw.parameter_a(), 0);
        assert_eq!(raw.parameter_b(), 0);
        assert_eq!(raw.value(), 0);

        raw.set_parameter_a(3);
        assert_eq!(raw.0[0], (1 << 28) | (2 << 24) | (3 << 16));
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 2);
        assert_eq!(raw.parameter_a(), 3);
        assert_eq!(raw.parameter_b(), 0);
        assert_eq!(raw.value(), 0);

        raw.set_parameter_b(4);
        assert_eq!(raw.0[0], (1 << 28) | (2 << 24) | (3 << 16) | 4);
        assert_eq!(raw.0[1], 0);
        assert_eq!(raw.condition(), 1);
        assert_eq!(raw.operation(), 2);
        assert_eq!(raw.parameter_a(), 3);
        assert_eq!(raw.parameter_b(), 4);
        assert_eq!(raw.value(), 0);

        raw.set_value(5);
        assert_eq!(raw.0[0], (1 << 28) | (2 << 24) | (3 << 16) | 4);
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
        assert_eq!(raw_instruction.0[0], 1 << 24);
        assert_eq!(raw_instruction.0[1], 0);
        assert_eq!(raw_instruction.operation(), 1)
    }

    #[test]
    fn test_goto_value() {
        let instruction = Instruction::Goto(Condition::Always, 0);
        let raw_instruction = instruction.to_raw();
        assert_eq!(raw_instruction.0[0], 2 << 24);
        assert_eq!(raw_instruction.0[1], 0);
        assert_eq!(raw_instruction.operation(), 2)
    }

    #[test]
    fn test_label_value() {
        let instruction = Instruction::Label(0);
        let raw_instruction = instruction.to_raw();
        assert_eq!(raw_instruction.0[0], 5 << 24);
        assert_eq!(raw_instruction.0[1], 0);
        assert_eq!(raw_instruction.operation(), 5)
    }

    #[test]
    fn test_complicated_value() {
        let instruction = Instruction::Goto(Condition::Equal(23, 1234), 42);
        let raw_instruction = instruction.to_raw();
        assert_eq!(raw_instruction.0[0], (1 << 28) | (2 << 24) | (42 << 16) | 23);
        assert_eq!(raw_instruction.0[1], 1234);
        assert_eq!(raw_instruction.condition(), 1);
        assert_eq!(raw_instruction.operation(), 2);
        assert_eq!(raw_instruction.parameter_a(), 42);
        assert_eq!(raw_instruction.parameter_b(), 23);
        assert_eq!(raw_instruction.value(), 1234);
    }
}
