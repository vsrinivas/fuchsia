// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bind_library::ValueType;
use crate::bind_program_v2_constants::*;
use crate::compiler::{
    BindProgram, BindProgramEncodeError, Symbol, SymbolTable, SymbolicInstructionInfo,
};
use crate::instruction::{Condition, Instruction};
use std::collections::HashMap;
use std::convert::TryFrom;

/// Functions for encoding the new bytecode format. When the
/// old bytecode format is deleted, the "v2" should be removed from the names.

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

struct Encoder<'a> {
    inst_iter: std::vec::IntoIter<SymbolicInstructionInfo<'a>>,
    symbol_table: SymbolTable,
    pub encoded_symbols: HashMap<String, u32>,
    label_map: HashMap<u32, LabelInfo>,
}

impl<'a> Encoder<'a> {
    pub fn new(bind_program: BindProgram<'a>) -> Self {
        Encoder {
            inst_iter: bind_program.instructions.into_iter(),
            symbol_table: bind_program.symbol_table,

            // Contains the key-value pairs in the symbol table.
            // Populated when the symbol tabel is encoded.
            encoded_symbols: HashMap::<String, u32>::new(),

            // Map of the label ID and the information. Used to
            // store the label location in the bytecode and to
            // calculate the jump offsets.
            label_map: HashMap::<u32, LabelInfo>::new(),
        }
    }

    pub fn encode_to_bytecode(mut self) -> Result<Vec<u8>, BindProgramEncodeError> {
        let mut bytecode: Vec<u8> = vec![];

        // Encode the header.
        bytecode.extend_from_slice(&BIND_MAGIC_NUM.to_be_bytes());
        bytecode.extend_from_slice(&BYTECODE_VERSION.to_le_bytes());

        // Encode the symbol table.
        let mut symbol_table = self.encode_symbol_table()?;
        bytecode.extend_from_slice(&SYMB_MAGIC_NUM.to_be_bytes());
        bytecode.extend_from_slice(&(symbol_table.len() as u32).to_le_bytes());
        bytecode.append(&mut symbol_table);

        // Encode the instruction section.
        let mut instruction_bytecode = self.encode_inst_block()?;
        bytecode.extend_from_slice(&INSTRUCTION_MAGIC_NUM.to_be_bytes());
        bytecode.extend_from_slice(&(instruction_bytecode.len() as u32).to_le_bytes());
        bytecode.append(&mut instruction_bytecode);

        Ok(bytecode)
    }

    fn encode_symbol_table(&mut self) -> Result<Vec<u8>, BindProgramEncodeError> {
        let mut bytecode: Vec<u8> = vec![];
        let mut unique_id: u32 = SYMB_TBL_START_KEY;

        // TODO(fxb/67919): Add support for enum values.
        for value in self.symbol_table.values() {
            if let Symbol::StringValue(str) = value {
                if str.len() > MAX_STRING_LENGTH {
                    return Err(BindProgramEncodeError::InvalidStringLength(str.to_string()));
                }

                // The strings in the symbol table contain fully qualified namespace, so
                // it's safe to assume that it won't contain duplicates in production.
                // However, as a precaution, panic if that happens.
                if self.encoded_symbols.contains_key(&str.to_string()) {
                    return Err(BindProgramEncodeError::DuplicateSymbol(str.to_string()));
                }

                self.encoded_symbols.insert(str.to_string(), unique_id);
                bytecode.extend_from_slice(&unique_id.to_le_bytes());

                // Encode the string followed by a zero terminator.
                bytecode.append(&mut str.to_string().into_bytes());
                bytecode.push(0);

                unique_id += 1;
            }
        }

        Ok(bytecode)
    }

    fn encode_inst_block(&mut self) -> Result<Vec<u8>, BindProgramEncodeError> {
        let mut bytecode: Vec<u8> = vec![];

        while let Some(symbolic_inst) = self.inst_iter.next() {
            let instruction = symbolic_inst.to_instruction().instruction;
            match instruction {
                Instruction::Abort(condition) => {
                    self.append_abort_instruction(&mut bytecode, condition)?;
                }
                Instruction::Goto(condition, label) => {
                    self.append_jmp_statement(&mut bytecode, condition, label)?;
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
        &self,
        bytecode: &mut Vec<u8>,
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
                self.append_value_comparison(bytecode, lhs, rhs)
            }
            Condition::NotEqual(lhs, rhs) => {
                bytecode.push(RawOp::EqualCondition as u8);
                self.append_value_comparison(bytecode, lhs, rhs)
            }
        }
    }

    fn append_jmp_statement(
        &mut self,
        bytecode: &mut Vec<u8>,
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
                self.append_value_comparison(bytecode, lhs, rhs)?;
            }
            Condition::NotEqual(lhs, rhs) => {
                bytecode.push(RawOp::JumpIfNotEqual as u8);
                bytecode.extend_from_slice(&placeholder_offset);
                self.append_value_comparison(bytecode, lhs, rhs)?;
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
        &self,
        bytecode: &mut Vec<u8>,
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
            Symbol::EnumValue => ValueType::Enum,
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

        self.append_value(bytecode, lhs)?;
        self.append_value(bytecode, rhs)?;
        Ok(())
    }

    fn append_value(
        &self,
        bytecode: &mut Vec<u8>,
        symbol: Symbol,
    ) -> Result<(), BindProgramEncodeError> {
        let (value_type, value) = match symbol {
            Symbol::NumberValue(value) => Ok((RawValueType::NumberValue as u8, value as u32)),
            Symbol::BoolValue(value) => Ok((RawValueType::BoolValue as u8, value as u32)),
            Symbol::StringValue(value) => {
                Ok((RawValueType::StringValue as u8, *self.lookup_symbol_table(value)?))
            }
            Symbol::Key(key, _) => Ok((RawValueType::Key as u8, *self.lookup_symbol_table(key)?)),
            Symbol::DeprecatedKey(key) => Ok((RawValueType::NumberValue as u8, key)),
            _ => unimplemented!("Unsupported symbol"),
        }?;

        bytecode.push(value_type);
        bytecode.extend_from_slice(&value.to_le_bytes());
        Ok(())
    }

    fn lookup_symbol_table(&self, value: String) -> Result<&u32, BindProgramEncodeError> {
        self.encoded_symbols
            .get(&value)
            .ok_or(BindProgramEncodeError::MissingStringInSymbolTable(value.to_string()))
    }
}

fn is_symbol_key(key: &Symbol) -> bool {
    match key {
        Symbol::DeprecatedKey(_) | Symbol::Key(_, _) => true,
        _ => false,
    }
}

pub fn encode_to_bytecode_v2(bind_program: BindProgram) -> Result<Vec<u8>, BindProgramEncodeError> {
    Encoder::new(bind_program).encode_to_bytecode()
}

pub fn encode_to_string_v2(
    bind_program: BindProgram,
) -> Result<(String, usize), BindProgramEncodeError> {
    let result = Encoder::new(bind_program).encode_to_bytecode()?;
    let byte_count = result.len();
    Ok((
        result.into_iter().map(|byte| format!("{:#x}", byte)).collect::<Vec<String>>().join(","),
        byte_count,
    ))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::compiler::{SymbolicInstruction, SymbolicInstructionInfo};
    use crate::make_identifier;
    use crate::parser_common::CompoundIdentifier;
    use std::collections::HashMap;

    // Constants representing the number of bytes in an operand and value.
    const OP_BYTES: u32 = 1;
    const VALUE_BYTES: u32 = 5;
    const OFFSET_BYTES: u32 = 4;

    // Constants representing the number of bytes in each instruction.
    const UNCOND_ABORT_BYTES: u32 = OP_BYTES;
    const COND_ABORT_BYTES: u32 = OP_BYTES + VALUE_BYTES + VALUE_BYTES;
    const UNCOND_JMP_BYTES: u32 = OP_BYTES + OFFSET_BYTES;
    const COND_JMP_BYTES: u32 = OP_BYTES + OFFSET_BYTES + VALUE_BYTES + VALUE_BYTES;
    const JMP_PAD_BYTES: u32 = OP_BYTES;

    struct EncodedValue {
        value_type: RawValueType,
        value: u32,
    }

    struct BytecodeChecker {
        iter: std::vec::IntoIter<u8>,
    }

    impl BytecodeChecker {
        pub fn new(bytecode: Vec<u8>) -> Self {
            BytecodeChecker { iter: bytecode.into_iter() }
        }

        fn verify_next_u8(&mut self, expected: u8) {
            assert_eq!(expected, self.iter.next().unwrap());
        }

        // Verify the expected value as little-endian and advance the iterator to the next four
        // bytes. This function shouldn't be used for magic numbers, which is in big-endian.
        fn verify_next_u32(&mut self, expected: u32) {
            let bytecode = expected.to_le_bytes();
            for i in &bytecode {
                self.verify_next_u8(*i);
            }
        }

        fn verify_magic_num(&mut self, expected: u32) {
            let bytecode = expected.to_be_bytes();
            for i in &bytecode {
                self.verify_next_u8(*i);
            }
        }

        // Verify that the next bytes matches the string and advance
        // the iterator.
        pub fn verify_string(&mut self, expected: String) {
            expected.chars().for_each(|c| self.verify_next_u8(c as u8));
            self.verify_next_u8(0);
        }

        fn verify_value(&mut self, val: EncodedValue) {
            self.verify_next_u8(val.value_type as u8);
            self.verify_next_u32(val.value);
        }

        pub fn verify_bind_program_header(&mut self) {
            self.verify_magic_num(BIND_MAGIC_NUM);
            self.verify_next_u32(BYTECODE_VERSION);
        }

        pub fn verify_sym_table_header(&mut self, num_of_bytes: u32) {
            self.verify_magic_num(SYMB_MAGIC_NUM);
            self.verify_next_u32(num_of_bytes);
        }

        pub fn verify_instructions_header(&mut self, num_of_bytes: u32) {
            self.verify_magic_num(INSTRUCTION_MAGIC_NUM);
            self.verify_next_u32(num_of_bytes);
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

        pub fn verify_jmp_if_not_equal(
            &mut self,
            offset: u32,
            lhs: EncodedValue,
            rhs: EncodedValue,
        ) {
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

    // Converts a vector of SymbolicInstruction into a vector of SymbolicInstructionInfo.
    // The location value for each element is set to None.
    fn to_symbolic_inst_info<'a>(
        instructions: Vec<SymbolicInstruction>,
    ) -> Vec<SymbolicInstructionInfo<'a>> {
        instructions
            .into_iter()
            .map(|inst| SymbolicInstructionInfo { location: None, instruction: inst })
            .collect()
    }

    #[test]
    fn test_symbol_table() {
        let mut symbol_table: SymbolTable = HashMap::new();

        symbol_table
            .insert(make_identifier!("cupwing"), Symbol::StringValue("wren-babbler".to_string()));
        symbol_table.insert(make_identifier!("shoveler"), Symbol::NumberValue(0));
        symbol_table.insert(make_identifier!("scoter"), Symbol::BoolValue(false));
        symbol_table.insert(make_identifier!("goldeneye"), Symbol::EnumValue);
        symbol_table.insert(make_identifier!("bufflehead"), Symbol::DeprecatedKey(0));
        symbol_table.insert(
            make_identifier!("canvasback"),
            Symbol::Key("redhead".to_string(), ValueType::Number),
        );
        symbol_table
            .insert(make_identifier!("nightjar"), Symbol::StringValue("frogmouth".to_string()));

        let symbol_table_values: Vec<String> = symbol_table
            .values()
            .filter_map(|symbol| match symbol {
                Symbol::StringValue(str) => Some(str.clone()),
                _ => None,
            })
            .collect();

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(vec![SymbolicInstruction::UnconditionalAbort]),
            symbol_table: symbol_table,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());
        checker.verify_bind_program_header();
        checker.verify_sym_table_header(31);

        // Only the symbols with string values should be in the symbol table.
        let mut unique_id = 1;
        symbol_table_values.into_iter().for_each(|value| {
            checker.verify_next_u32(unique_id);
            checker.verify_string(value);
            unique_id += 1;
        });

        checker.verify_instructions_header(UNCOND_ABORT_BYTES);
        checker.verify_unconditional_abort();
        checker.verify_end();
    }

    #[test]
    fn test_empty_symbol_table() {
        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(vec![SymbolicInstruction::UnconditionalAbort]),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());

        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);
        checker.verify_instructions_header(UNCOND_ABORT_BYTES);
        checker.verify_unconditional_abort();
        checker.verify_end();
    }

    #[test]
    fn test_duplicates_in_symbol_table() {
        let mut symbol_table: SymbolTable = HashMap::new();
        symbol_table
            .insert(make_identifier!("curlew"), Symbol::StringValue("sandpiper".to_string()));
        symbol_table
            .insert(make_identifier!("turnstone"), Symbol::StringValue("sandpiper".to_string()));

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(vec![SymbolicInstruction::UnconditionalAbort]),
            symbol_table: symbol_table,
        };

        assert_eq!(
            Err(BindProgramEncodeError::DuplicateSymbol("sandpiper".to_string())),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_long_string_in_symbol_table() {
        let mut symbol_table: SymbolTable = HashMap::new();

        let long_str = "loooooooooooooooooooooooooo\
                        oooooooooooooooooooooooooooo\
                        ooooooooooooooooooooooooooo\
                        ooooooooooooooooooooooong, \
                        loooooooooooooooooooooooooo\
                        ooooooooooooooooooooooooooo\
                        ooooooooooooooooooooooooooo\
                        ooooooooooooooooooooooooooo\
                        ooooooooooooooooooooooooooo\
                        oooooooong string";

        symbol_table
            .insert(make_identifier!("long"), Symbol::StringValue(long_str.clone().to_string()));

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(vec![SymbolicInstruction::UnconditionalAbort]),
            symbol_table: symbol_table,
        };

        assert_eq!(
            Err(BindProgramEncodeError::InvalidStringLength(long_str.to_string())),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_abort_instructions() {
        let instructions = vec![
            SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::NumberValue(100),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(1),
                rhs: Symbol::BoolValue(false),
            },
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());
        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);

        checker.verify_instructions_header(COND_ABORT_BYTES + COND_ABORT_BYTES);
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 100 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 0 },
        );
        checker.verify_end();
    }

    #[test]
    fn test_missing_string_in_symbol_table() {
        // Test with missing string in the string value.
        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::DeprecatedKey(10),
            rhs: Symbol::StringValue("treecreeper".to_string()),
        }];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::MissingStringInSymbolTable("treecreeper".to_string())),
            encode_to_bytecode_v2(bind_program)
        );

        // Test with missing string in the key.
        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::Key("wallcreeper".to_string(), ValueType::Number),
            rhs: Symbol::NumberValue(10),
        }];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::MissingStringInSymbolTable("wallcreeper".to_string())),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_unconditional_jump_statement() {
        let instructions = vec![
            SymbolicInstruction::UnconditionalJump { label: 1 },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());
        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);

        checker.verify_instructions_header(UNCOND_JMP_BYTES + UNCOND_ABORT_BYTES + JMP_PAD_BYTES);
        checker.verify_unconditional_jmp(UNCOND_ABORT_BYTES);
        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();
        checker.verify_end();
    }

    #[test]
    fn test_jump_if_equal_statement() {
        let instructions = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(15),
                rhs: Symbol::NumberValue(12),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(10),
                rhs: Symbol::NumberValue(10),
            },
            SymbolicInstruction::Label(1),
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());
        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);

        // Verify the instructions.
        checker.verify_instructions_header(
            COND_JMP_BYTES + UNCOND_ABORT_BYTES + COND_ABORT_BYTES + JMP_PAD_BYTES,
        );
        checker.verify_jmp_if_equal(
            UNCOND_ABORT_BYTES + COND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 12 },
        );
        checker.verify_unconditional_abort();
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
        );
        checker.verify_jmp_pad();

        checker.verify_end();
    }

    #[test]
    fn test_jump_if_not_equal_statement() {
        let instructions = vec![
            SymbolicInstruction::JumpIfNotEqual {
                lhs: Symbol::DeprecatedKey(15),
                rhs: Symbol::BoolValue(true),
                label: 2,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::NumberValue(7),
            },
            SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::DeprecatedKey(2),
                rhs: Symbol::BoolValue(true),
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(2),
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());
        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);

        let expected_bytes =
            COND_JMP_BYTES + (UNCOND_ABORT_BYTES * 2) + (COND_ABORT_BYTES * 2) + JMP_PAD_BYTES;
        checker.verify_instructions_header(expected_bytes);

        // Verify Jump If Not Equal.
        let expected_offset = (UNCOND_ABORT_BYTES * 2) + (COND_ABORT_BYTES * 2);
        checker.verify_jmp_if_not_equal(
            expected_offset,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 1 },
        );

        // Verify abort statements.
        checker.verify_unconditional_abort();
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 7 },
        );
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 2 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 1 },
        );
        checker.verify_unconditional_abort();

        checker.verify_jmp_pad();
        checker.verify_end();
    }

    #[test]
    fn test_instructions_with_strings() {
        let instructions = vec![
            SymbolicInstruction::JumpIfNotEqual {
                lhs: Symbol::Key("shining sunbeam".to_string(), ValueType::Str),
                rhs: Symbol::StringValue("bearded mountaineer".to_string()),
                label: 1,
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("puffleg".to_string(), ValueType::Str),
                rhs: Symbol::StringValue("bearded mountaineer".to_string()),
            },
            SymbolicInstruction::Label(1),
        ];

        let mut symbol_table: SymbolTable = HashMap::new();
        symbol_table.insert(
            make_identifier!("aglaeactis"),
            Symbol::StringValue("shining sunbeam".to_string()),
        );
        symbol_table.insert(
            make_identifier!("oreonympha"),
            Symbol::StringValue("bearded mountaineer".to_string()),
        );
        symbol_table
            .insert(make_identifier!("eriocnemis"), Symbol::StringValue("puffleg".to_string()));

        let symbol_table_values: Vec<String> = symbol_table
            .values()
            .filter_map(|symbol| match symbol {
                Symbol::StringValue(str) => Some(str.clone()),
                _ => None,
            })
            .collect();

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: symbol_table,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());
        checker.verify_bind_program_header();

        checker.verify_sym_table_header(56);
        let mut unique_id = 1;
        let mut sym_map: HashMap<String, u32> = HashMap::<String, u32>::new();
        symbol_table_values.iter().for_each(|value| {
            checker.verify_next_u32(unique_id);
            checker.verify_string(value.clone());
            sym_map.insert(value.clone(), unique_id);
            unique_id += 1;
        });

        checker.verify_instructions_header(COND_JMP_BYTES + COND_ABORT_BYTES + JMP_PAD_BYTES);
        checker.verify_jmp_if_not_equal(
            COND_ABORT_BYTES,
            EncodedValue {
                value_type: RawValueType::Key,
                value: *sym_map.get(&"shining sunbeam".to_string()).unwrap(),
            },
            EncodedValue {
                value_type: RawValueType::StringValue,
                value: *sym_map.get(&"bearded mountaineer".to_string()).unwrap(),
            },
        );
        checker.verify_abort_equal(
            EncodedValue {
                value_type: RawValueType::Key,
                value: *sym_map.get(&"puffleg".to_string()).unwrap(),
            },
            EncodedValue {
                value_type: RawValueType::StringValue,
                value: *sym_map.get(&"bearded mountaineer".to_string()).unwrap(),
            },
        );

        checker.verify_jmp_pad();
        checker.verify_end();
    }

    #[test]
    fn test_nested_jump_statement() {
        let instructions = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(10),
                rhs: Symbol::NumberValue(11),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::UnconditionalJump { label: 2 },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::NumberValue(7),
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(2),
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());

        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);

        // Verify the instructions.
        let nested_jmp_block_bytes =
            UNCOND_JMP_BYTES + COND_ABORT_BYTES + UNCOND_ABORT_BYTES + JMP_PAD_BYTES;
        let instructions_bytes = COND_JMP_BYTES
            + UNCOND_ABORT_BYTES
            + nested_jmp_block_bytes
            + UNCOND_ABORT_BYTES
            + JMP_PAD_BYTES;
        checker.verify_instructions_header(instructions_bytes);

        // Verify Jump If Equal.
        let jmp_offset = UNCOND_ABORT_BYTES + nested_jmp_block_bytes + UNCOND_ABORT_BYTES;
        checker.verify_jmp_if_equal(
            jmp_offset,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 11 },
        );
        checker.verify_unconditional_abort();

        // Verify the nested jump block.
        checker.verify_unconditional_jmp(COND_ABORT_BYTES + UNCOND_ABORT_BYTES);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 7 },
        );
        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();

        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();
        checker.verify_end();
    }

    #[test]
    fn test_overlapping_jump_statements() {
        let instructions = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(10),
                rhs: Symbol::NumberValue(11),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::UnconditionalJump { label: 2 },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::NumberValue(7),
            },
            SymbolicInstruction::Label(1),
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(2),
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());
        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);

        let instructions_bytes = COND_JMP_BYTES
            + UNCOND_ABORT_BYTES
            + UNCOND_JMP_BYTES
            + COND_ABORT_BYTES
            + JMP_PAD_BYTES
            + UNCOND_ABORT_BYTES
            + JMP_PAD_BYTES;
        checker.verify_instructions_header(instructions_bytes);

        let jmp_offset = UNCOND_ABORT_BYTES + UNCOND_JMP_BYTES + COND_ABORT_BYTES;
        checker.verify_jmp_if_equal(
            jmp_offset,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 11 },
        );
        checker.verify_unconditional_abort();

        let jmp_offset = COND_ABORT_BYTES + JMP_PAD_BYTES + UNCOND_ABORT_BYTES;
        checker.verify_unconditional_jmp(jmp_offset);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 7 },
        );

        checker.verify_jmp_pad();
        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();

        checker.verify_end();
    }

    #[test]
    fn test_same_label_statements() {
        let instructions = vec![
            SymbolicInstruction::UnconditionalJump { label: 1 },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::NumberValue(7),
            },
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(10),
                rhs: Symbol::NumberValue(11),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());
        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);

        let instructions_bytes = UNCOND_JMP_BYTES
            + COND_ABORT_BYTES
            + COND_JMP_BYTES
            + UNCOND_ABORT_BYTES
            + JMP_PAD_BYTES;
        checker.verify_instructions_header(instructions_bytes);

        checker.verify_unconditional_jmp(COND_ABORT_BYTES + COND_JMP_BYTES + UNCOND_ABORT_BYTES);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 7 },
        );
        checker.verify_jmp_if_equal(
            UNCOND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 11 },
        );
        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();
        checker.verify_end();
    }

    #[test]
    fn test_duplicate_label() {
        let instructions = vec![
            SymbolicInstruction::UnconditionalJump { label: 1 },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::NumberValue(7),
            },
            SymbolicInstruction::Label(1),
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::DuplicateLabel(1)),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_unused_label() {
        let instructions = vec![
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
            SymbolicInstruction::UnconditionalAbort,
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());

        checker.verify_bind_program_header();
        checker.verify_sym_table_header(0);

        checker.verify_instructions_header(UNCOND_ABORT_BYTES + JMP_PAD_BYTES + UNCOND_ABORT_BYTES);
        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();
        checker.verify_unconditional_abort();
    }

    #[test]
    fn test_label_appears_before_jmp() {
        let instructions = vec![
            SymbolicInstruction::Label(1),
            SymbolicInstruction::UnconditionalJump { label: 1 },
            SymbolicInstruction::UnconditionalAbort,
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::InvalidGotoLocation(1)),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_missing_label() {
        let instructions = vec![
            SymbolicInstruction::UnconditionalJump { label: 2 },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::MissingLabel(2)),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_mismatch_value_types() {
        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::Key("waxwing".to_string(), ValueType::Number),
            rhs: Symbol::BoolValue(true),
        }];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::MismatchValueTypes(ValueType::Number, ValueType::Bool)),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_invalid_lhs_symbol() {
        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::NumberValue(5),
            rhs: Symbol::BoolValue(true),
        }];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::IncorrectTypesInValueComparison),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_invalid_rhs_symbol() {
        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::DeprecatedKey(5),
            rhs: Symbol::DeprecatedKey(6),
        }];
        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };
        assert_eq!(
            Err(BindProgramEncodeError::IncorrectTypesInValueComparison),
            encode_to_bytecode_v2(bind_program)
        );

        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::DeprecatedKey(5),
            rhs: Symbol::Key("wagtail".to_string(), ValueType::Number),
        }];
        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };
        assert_eq!(
            Err(BindProgramEncodeError::IncorrectTypesInValueComparison),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_missing_match_instruction() {
        let instructions =
            vec![SymbolicInstruction::UnconditionalAbort, SymbolicInstruction::UnconditionalBind];

        let bind_program = BindProgram {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::MatchNotSupported),
            encode_to_bytecode_v2(bind_program)
        );
    }
}
