// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bind_library::ValueType;
use crate::bind_program_v2_constants::*;
use crate::compiler::{BindProgramEncodeError, Symbol, SymbolicInstructionInfo};
use crate::instruction::{Condition, Instruction};
use crate::symbol_table_encoder::SymbolTableEncoder;
use std::collections::HashMap;
use std::convert::TryFrom;

// Info on a jump instruction's offset. |index| represents the jump offset's
// location in the bytecode vector. |inst_offset| represents number of bytes
// |index| is from the end of the jump instruction. The jump offset is
// calculated by subtracting |index| and |inst_offset| from label location.
struct JumpInstructionOffsetInfo {
    index: usize,
    inst_offset: usize,
}

struct LabelInfo {
    pub index: Option<usize>,
    pub jump_instructions: Vec<JumpInstructionOffsetInfo>,
}

fn is_symbol_key(key: &Symbol) -> bool {
    match key {
        Symbol::DeprecatedKey(_) | Symbol::Key(_, _) => true,
        _ => false,
    }
}

pub struct InstructionEncoder<'a> {
    inst_iter: std::vec::IntoIter<SymbolicInstructionInfo<'a>>,
    label_map: HashMap<u32, LabelInfo>,
}

impl<'a> InstructionEncoder<'a> {
    pub fn new(instructions: Vec<SymbolicInstructionInfo<'a>>) -> Self {
        InstructionEncoder {
            inst_iter: instructions.into_iter(),

            // Map of the label ID and the information. Used to
            // store the label location in the bytecode and to
            // calculate the jump offsets.
            label_map: HashMap::<u32, LabelInfo>::new(),
        }
    }

    pub fn encode(
        &mut self,
        symbol_table_encoder: &mut SymbolTableEncoder,
    ) -> Result<Vec<u8>, BindProgramEncodeError> {
        let mut bytecode: Vec<u8> = vec![];

        while let Some(symbolic_inst) = self.inst_iter.next() {
            let instruction = symbolic_inst.to_instruction().instruction;
            match instruction {
                Instruction::Abort(condition) => {
                    self.append_abort_instruction(&mut bytecode, symbol_table_encoder, condition)?;
                }
                Instruction::Goto(condition, label) => {
                    self.append_jmp_statement(
                        &mut bytecode,
                        symbol_table_encoder,
                        condition,
                        label,
                    )?;
                }
                Instruction::Label(label_id) => {
                    self.append_and_update_label(&mut bytecode, label_id)?;
                }
                Instruction::Match(_) => {
                    // Match statements are not supported in the new bytecode. Once
                    // the old bytecode is removed, they can be deleted.
                    return Err(BindProgramEncodeError::MatchNotSupported);
                }
            };
        }

        // Update the jump instruction offsets.
        for (label_id, data) in self.label_map.iter() {
            // If the label index is not available, then the label is missing in the bind program.
            if data.index.is_none() {
                return Err(BindProgramEncodeError::MissingLabel(*label_id));
            }

            let label_index = data.index.unwrap();
            for usage in data.jump_instructions.iter() {
                let offset = u32::try_from(label_index - usage.index - usage.inst_offset)
                    .map_err(|_| BindProgramEncodeError::JumpOffsetOutOfRange(*label_id))?;

                let offset_bytes = offset.to_le_bytes();
                for i in 0..4 {
                    bytecode[usage.index + i] = offset_bytes[i];
                }
            }
        }

        Ok(bytecode)
    }

    fn append_abort_instruction(
        &mut self,
        bytecode: &mut Vec<u8>,
        symbol_table_encoder: &mut SymbolTableEncoder,
        condition: Condition,
    ) -> Result<(), BindProgramEncodeError> {
        // Since the bind program aborts when a condition statement fails, we encode the opposite
        // condition in the Abort instruction. For example, if the given condition is Equal, we
        // would encode AbortIfNotEqual.
        match condition {
            Condition::Always => {
                bytecode.push(RawOp::Abort as u8);
                Ok(())
            }
            Condition::Equal(lhs, rhs) => {
                bytecode.push(RawOp::InequalCondition as u8);
                self.append_value_comparison(bytecode, symbol_table_encoder, lhs, rhs)
            }
            Condition::NotEqual(lhs, rhs) => {
                bytecode.push(RawOp::EqualCondition as u8);
                self.append_value_comparison(bytecode, symbol_table_encoder, lhs, rhs)
            }
        }
    }

    fn append_jmp_statement(
        &mut self,
        bytecode: &mut Vec<u8>,
        symbol_table_encoder: &mut SymbolTableEncoder,
        condition: Condition,
        label_id: u32,
    ) -> Result<(), BindProgramEncodeError> {
        let offset_index = bytecode.len() + 1;
        let placeholder_offset = (0 as u32).to_le_bytes();
        match condition {
            Condition::Always => {
                bytecode.push(RawOp::UnconditionalJump as u8);
                bytecode.extend_from_slice(&placeholder_offset);
            }
            Condition::Equal(lhs, rhs) => {
                bytecode.push(RawOp::JumpIfEqual as u8);
                bytecode.extend_from_slice(&placeholder_offset);
                self.append_value_comparison(bytecode, symbol_table_encoder, lhs, rhs)?;
            }
            Condition::NotEqual(lhs, rhs) => {
                bytecode.push(RawOp::JumpIfNotEqual as u8);
                bytecode.extend_from_slice(&placeholder_offset);
                self.append_value_comparison(bytecode, symbol_table_encoder, lhs, rhs)?;
            }
        };

        // If the label's index is already set, then the label appears before
        // the jump statement. We can make this assumption because we're
        // encoding the bind program in one direction.
        if let Some(data) = self.label_map.get(&label_id) {
            if data.index.is_some() {
                return Err(BindProgramEncodeError::InvalidGotoLocation(label_id));
            }
        }

        // Add the label to the map if it doesn't already exists. Push the jump instruction
        // offset to the map.
        self.label_map
            .entry(label_id)
            .or_insert(LabelInfo { index: None, jump_instructions: vec![] })
            .jump_instructions
            .push(JumpInstructionOffsetInfo {
                index: offset_index,
                inst_offset: bytecode.len() - offset_index,
            });

        Ok(())
    }

    fn append_and_update_label(
        &mut self,
        bytecode: &mut Vec<u8>,
        label_id: u32,
    ) -> Result<(), BindProgramEncodeError> {
        if let Some(data) = self.label_map.get(&label_id) {
            if data.index.is_some() {
                return Err(BindProgramEncodeError::DuplicateLabel(label_id));
            }
        }

        self.label_map
            .entry(label_id)
            .and_modify(|data| data.index = Some(bytecode.len()))
            .or_insert(LabelInfo { index: Some(bytecode.len()), jump_instructions: vec![] });

        bytecode.push(RawOp::JumpLandPad as u8);
        Ok(())
    }

    fn append_value_comparison(
        &mut self,
        bytecode: &mut Vec<u8>,
        symbol_table_encoder: &mut SymbolTableEncoder,
        lhs: Symbol,
        rhs: Symbol,
    ) -> Result<(), BindProgramEncodeError> {
        // LHS value should represent a key.
        if !is_symbol_key(&lhs) {
            return Err(BindProgramEncodeError::IncorrectTypesInValueComparison);
        }

        let rhs_val_type = match rhs {
            Symbol::NumberValue(_) => ValueType::Number,
            Symbol::StringValue(_) => ValueType::Str,
            Symbol::BoolValue(_) => ValueType::Bool,
            Symbol::EnumValue(_) => ValueType::Enum,
            _ => {
                // The RHS value should not represent a key.
                return Err(BindProgramEncodeError::IncorrectTypesInValueComparison);
            }
        };

        // If the LHS key contains a value type, compare it to the RHS value to ensure that the
        // types match.
        if let Symbol::Key(_, lhs_val_type) = lhs {
            if lhs_val_type != rhs_val_type {
                return Err(BindProgramEncodeError::MismatchValueTypes(lhs_val_type, rhs_val_type));
            }
        }

        self.append_value(bytecode, symbol_table_encoder, lhs)?;
        self.append_value(bytecode, symbol_table_encoder, rhs)?;
        Ok(())
    }

    fn append_value(
        &mut self,
        bytecode: &mut Vec<u8>,
        symbol_table_encoder: &mut SymbolTableEncoder,
        symbol: Symbol,
    ) -> Result<(), BindProgramEncodeError> {
        let (value_type, value) = match symbol {
            Symbol::NumberValue(value) => Ok((RawValueType::NumberValue as u8, value as u32)),
            Symbol::BoolValue(value) => Ok((RawValueType::BoolValue as u8, value as u32)),
            Symbol::StringValue(value) => {
                Ok((RawValueType::StringValue as u8, symbol_table_encoder.get_key(value)?))
            }
            Symbol::Key(key, _) => {
                Ok((RawValueType::Key as u8, symbol_table_encoder.get_key(key)?))
            }
            Symbol::DeprecatedKey(key) => Ok((RawValueType::NumberValue as u8, key)),
            Symbol::EnumValue(value) => {
                Ok((RawValueType::EnumValue as u8, symbol_table_encoder.get_key(value)?))
            }
        }?;

        bytecode.push(value_type);
        bytecode.extend_from_slice(&value.to_le_bytes());
        Ok(())
    }
}

// Encode the instructions into bytecode. Add symbols into
// |symbol_table_encoder|.
pub fn encode_instructions<'a>(
    instructions: Vec<SymbolicInstructionInfo<'a>>,
    symbol_table_encoder: &mut SymbolTableEncoder,
) -> Result<Vec<u8>, BindProgramEncodeError> {
    InstructionEncoder::new(instructions).encode(symbol_table_encoder)
}
