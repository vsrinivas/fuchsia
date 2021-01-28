// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler::{
    BindProgram, BindProgramEncodeError, Symbol, SymbolTable, SymbolicInstructionInfo,
};
use crate::instruction::{Condition, Instruction};
use std::collections::HashMap;

/// Functions for encoding the new bytecode format. When the
/// old bytecode format is deleted, the "v2" should be removed from the names.

// Magic number for BIND.
const BIND_MAGIC_NUM: u32 = 0x42494E44;

// Magic number for SYMB.
const SYMB_MAGIC_NUM: u32 = 0x53594E42;

// Magic number for INST.
const INSTRUCTION_MAGIC_NUM: u32 = 0x494E5354;

const BYTECODE_VERSION: u32 = 2;

#[allow(dead_code)]
enum RawOp {
    EqualCondition = 0x01,
    InequalCondition = 0x02,
    UnconditionalJump = 0x10,
    JumpIfEqual = 0x11,
    JumpIfNotEqual = 0x12,
    JumpLandPad = 0x20,
    Abort = 0x30,
    DebugStart = 0x40,
    DebugTerminate = 0x41,
}

#[allow(dead_code)]
enum RawValueType {
    Key = 0,
    NumberValue,
    StringValue,
    BoolValue,
    EnumValue,
}

struct Encoder<'a> {
    inst_iter: std::vec::IntoIter<SymbolicInstructionInfo<'a>>,
    symbol_table: SymbolTable,
    pub encoded_symbols: HashMap<String, u32>,
}

impl<'a> Encoder<'a> {
    pub fn new(bind_program: BindProgram<'a>) -> Self {
        Encoder {
            inst_iter: bind_program.instructions.into_iter(),
            symbol_table: bind_program.symbol_table,
            encoded_symbols: HashMap::<String, u32>::new(),
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
        let mut unique_id: u32 = 1;

        // TODO(fxb/67919): Add support for enum values.
        for value in self.symbol_table.values() {
            if let Symbol::StringValue(str) = value {
                // The max string length is 255 characters.
                if str.len() > 255 {
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

            // TODO(fxb/67440): Handle and encode goto statements.
            match instruction {
                Instruction::Abort(condition) => {
                    self.append_abort_instruction(&mut bytecode, condition)?
                }
                _ => {}
            };
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

    fn append_value_comparison(
        &self,
        bytecode: &mut Vec<u8>,
        lhs: Symbol,
        rhs: Symbol,
    ) -> Result<(), BindProgramEncodeError> {
        // For the comparison to be valid, the value types need to match.
        if std::mem::discriminant(&lhs) != std::mem::discriminant(&rhs) {
            return Err(BindProgramEncodeError::MismatchValueTypes(lhs, rhs));
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
                let key = self
                    .encoded_symbols
                    .get(&value)
                    .ok_or(BindProgramEncodeError::MissingStringInSymbolTable(value.to_string()))?;
                Ok((RawValueType::StringValue as u8, *key))
            }
            _ => unimplemented!("Unsupported symbol"),
        }?;

        bytecode.push(value_type);
        bytecode.extend_from_slice(&value.to_le_bytes());
        Ok(())
    }
}

pub fn encode_to_bytecode_v2(bind_program: BindProgram) -> Result<Vec<u8>, BindProgramEncodeError> {
    Encoder::new(bind_program).encode_to_bytecode()
}

pub fn encode_to_string_v2(_bind_program: BindProgram) -> Result<String, BindProgramEncodeError> {
    // Unimplemented, new bytecode is not implemented yet. See fxb/67440.
    Ok("".to_string())
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bind_library::ValueType;
    use crate::compiler::{SymbolicInstruction, SymbolicInstructionInfo};
    use crate::make_identifier;
    use crate::parser_common::CompoundIdentifier;
    use std::collections::HashMap;

    struct BytecodeChecker {
        iter: std::vec::IntoIter<u8>,
    }

    impl BytecodeChecker {
        pub fn new(bytecode: Vec<u8>) -> Self {
            BytecodeChecker { iter: bytecode.into_iter() }
        }

        pub fn verify_next_u8(&mut self, expected: u8) {
            assert_eq!(expected, self.iter.next().unwrap());
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

        // Verify that the next bytes matches the string and advance
        // the iterator.
        pub fn verify_string(&mut self, expected: String) {
            expected.chars().for_each(|c| self.verify_next_u8(c as u8));
            self.verify_next_u8(0);
        }

        pub fn verify_value(&mut self, expected_type: RawValueType, expected_value: u32) {
            self.verify_next_u8(expected_type as u8);
            self.verify_next_u32(expected_value);
        }

        // Verify that the iterator reached the end of the bytecode.
        pub fn verify_end(&mut self) {
            assert_eq!(None, self.iter.next());
        }
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
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalAbort,
            }],
            symbol_table: symbol_table,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());

        // Verify the header.
        checker.verify_magic_num(BIND_MAGIC_NUM);
        checker.verify_next_u32(BYTECODE_VERSION);

        // Verify symbol table. Only the symbols with string values should
        // be in the table.
        checker.verify_magic_num(SYMB_MAGIC_NUM);
        checker.verify_next_u32(31);

        let mut unique_id = 1;
        symbol_table_values.into_iter().for_each(|value| {
            checker.verify_next_u32(unique_id);
            checker.verify_string(value);
            unique_id += 1;
        });

        // Verify the instruction section.
        checker.verify_magic_num(INSTRUCTION_MAGIC_NUM);
        checker.verify_next_u32(1);
        checker.verify_next_u8(0x30);

        checker.verify_end();
    }

    #[test]
    fn test_empty_symbol_table() {
        let bind_program = BindProgram {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalAbort,
            }],
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());

        // Verify the header.
        checker.verify_magic_num(BIND_MAGIC_NUM);
        checker.verify_next_u32(BYTECODE_VERSION);

        // Verify symbol table. Only the symbols with string values should
        // be in the table.
        checker.verify_magic_num(SYMB_MAGIC_NUM);
        checker.verify_next_u32(0);

        // Verify the instruction section.
        checker.verify_magic_num(INSTRUCTION_MAGIC_NUM);
        checker.verify_next_u32(1);
        checker.verify_next_u8(0x30);

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
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalAbort,
            }],
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
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalAbort,
            }],
            symbol_table: symbol_table,
        };

        assert_eq!(
            Err(BindProgramEncodeError::InvalidStringLength(long_str.to_string())),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_abort_instructions() {
        let bind_program = BindProgram {
            instructions: vec![
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::AbortIfNotEqual {
                        lhs: Symbol::NumberValue(5),
                        rhs: Symbol::NumberValue(100),
                    },
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::AbortIfEqual {
                        lhs: Symbol::BoolValue(true),
                        rhs: Symbol::BoolValue(false),
                    },
                },
            ],
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_program).unwrap());

        // Check Bind header section.
        checker.verify_magic_num(BIND_MAGIC_NUM);
        checker.verify_next_u32(BYTECODE_VERSION);

        // Verify the symbol table.
        checker.verify_magic_num(SYMB_MAGIC_NUM);
        checker.verify_next_u32(0);

        // Check Instruction section.
        checker.verify_magic_num(INSTRUCTION_MAGIC_NUM);
        checker.verify_next_u32(22);

        // Verify AbortIfNotEqual instruction.
        checker.verify_next_u8(0x01);
        checker.verify_value(RawValueType::NumberValue, 5);
        checker.verify_value(RawValueType::NumberValue, 100);

        // Verify AbortIfNotEqual instruction.
        checker.verify_next_u8(0x02);
        checker.verify_value(RawValueType::BoolValue, 1);
        checker.verify_value(RawValueType::BoolValue, 0);

        checker.verify_end();
    }

    #[test]
    fn test_mismatch_value_types() {
        let bind_program = BindProgram {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::AbortIfNotEqual {
                    lhs: Symbol::NumberValue(5),
                    rhs: Symbol::BoolValue(true),
                },
            }],
            symbol_table: HashMap::new(),
        };
        assert_eq!(
            Err(BindProgramEncodeError::MismatchValueTypes(
                Symbol::NumberValue(5),
                Symbol::BoolValue(true)
            )),
            encode_to_bytecode_v2(bind_program)
        );

        let bind_program = BindProgram {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::AbortIfNotEqual {
                    lhs: Symbol::StringValue("5".to_string()),
                    rhs: Symbol::NumberValue(5),
                },
            }],
            symbol_table: HashMap::new(),
        };
        assert_eq!(
            Err(BindProgramEncodeError::MismatchValueTypes(
                Symbol::StringValue("5".to_string()),
                Symbol::NumberValue(5)
            )),
            encode_to_bytecode_v2(bind_program)
        );
    }

    #[test]
    fn test_missing_string_in_symbol_table() {
        let bind_program = BindProgram {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::AbortIfNotEqual {
                    lhs: Symbol::StringValue("wallcreeper".to_string()),
                    rhs: Symbol::StringValue("treecreeper".to_string()),
                },
            }],
            symbol_table: HashMap::new(),
        };

        assert_eq!(
            Err(BindProgramEncodeError::MissingStringInSymbolTable("wallcreeper".to_string())),
            encode_to_bytecode_v2(bind_program)
        );
    }
}
