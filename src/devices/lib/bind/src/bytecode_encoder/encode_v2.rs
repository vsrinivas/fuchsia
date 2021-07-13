// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bytecode_constants::*;
use crate::bytecode_encoder::error::BindRulesEncodeError;
use crate::bytecode_encoder::instruction_encoder::encode_instructions;
use crate::bytecode_encoder::symbol_table_encoder::SymbolTableEncoder;
use crate::compiler::{BindRules, CompositeBindRules};

/// Functions for encoding the new bytecode format. When the
/// old bytecode format is deleted, the "v2" should be removed from the names.

pub fn encode_to_bytecode_v2(bind_rules: BindRules) -> Result<Vec<u8>, BindRulesEncodeError> {
    let mut symbol_table_encoder = SymbolTableEncoder::new();
    let mut instruction_bytecode =
        encode_instructions(bind_rules.instructions, &mut symbol_table_encoder)?;

    let mut bytecode: Vec<u8> = vec![];

    // Encode the header.
    bytecode.extend_from_slice(&BIND_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&BYTECODE_VERSION.to_le_bytes());

    // Encode the symbol table.
    bytecode.extend_from_slice(&SYMB_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&(symbol_table_encoder.bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut symbol_table_encoder.bytecode);

    // Encode the instruction section.
    bytecode.extend_from_slice(&INSTRUCTION_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&(instruction_bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut instruction_bytecode);

    Ok(bytecode)
}

pub fn encode_to_string_v2(bind_rules: BindRules) -> Result<(String, usize), BindRulesEncodeError> {
    let result = encode_to_bytecode_v2(bind_rules)?;
    let byte_count = result.len();
    Ok((
        result.into_iter().map(|byte| format!("{:#x}", byte)).collect::<Vec<String>>().join(","),
        byte_count,
    ))
}

pub fn encode_composite_to_bytecode(
    bind_rules: CompositeBindRules,
) -> Result<Vec<u8>, BindRulesEncodeError> {
    let mut symbol_table_encoder = SymbolTableEncoder::new();
    if bind_rules.device_name.is_empty() {
        return Err(BindRulesEncodeError::MissingCompositeDeviceName);
    }

    // Instruction bytecode begins with the device name ID.
    let device_name_id = symbol_table_encoder.get_key(bind_rules.device_name)?;
    let mut inst_bytecode = device_name_id.to_le_bytes().to_vec();

    // Add instructions from the primary node.
    let mut primary_node_bytecode =
        encode_instructions(bind_rules.primary_node_instructions, &mut symbol_table_encoder)?;
    inst_bytecode.push(RawNodeType::Primary as u8);
    inst_bytecode.extend_from_slice(&(primary_node_bytecode.len() as u32).to_le_bytes());
    inst_bytecode.append(&mut primary_node_bytecode);

    // Add instructions from additional nodes.
    for node_insts in bind_rules.additional_node_instructions.into_iter() {
        let mut node_bytecode = encode_instructions(node_insts, &mut symbol_table_encoder)?;
        inst_bytecode.push(RawNodeType::Additional as u8);
        inst_bytecode.extend_from_slice(&(node_bytecode.len() as u32).to_le_bytes());
        inst_bytecode.append(&mut node_bytecode);
    }

    // Put all of the sections together.
    let mut bytecode: Vec<u8> = vec![];

    // Encode the header.
    bytecode.extend_from_slice(&BIND_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&BYTECODE_VERSION.to_le_bytes());

    // Encode the symbol table.
    bytecode.extend_from_slice(&SYMB_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&(symbol_table_encoder.bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut symbol_table_encoder.bytecode);

    // Encode the instruction section.
    bytecode.extend_from_slice(&COMPOSITE_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&(inst_bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut inst_bytecode);

    Ok(bytecode)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::compiler::Symbol;
    use crate::compiler::{SymbolicInstruction, SymbolicInstructionInfo};
    use crate::parser::bind_library::ValueType;
    use std::collections::HashMap;

    // Constants representing the number of bytes in an operand and value.
    const OP_BYTES: u32 = 1;
    const VALUE_BYTES: u32 = 5;
    const OFFSET_BYTES: u32 = 4;

    const NODE_HEADER_BYTES: u32 = 5;
    const COMPOSITE_NAME_ID_BYTES: u32 = 4;

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

        fn verify_node_type(&mut self, node_type: RawNodeType, num_of_bytes: u32) {
            self.verify_next_u8(node_type as u8);
            self.verify_next_u32(num_of_bytes);
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

        pub fn verify_bind_rules_header(&mut self) {
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

        pub fn verify_composite_header(&mut self, num_of_bytes: u32) {
            self.verify_magic_num(COMPOSITE_MAGIC_NUM);
            self.verify_next_u32(num_of_bytes);

            // The device name ID is the first one in the symbol table.
            // This isn't required in the bytecode specs, it's just how it's
            // implemented in encode_composite_to_bytecode().
            self.verify_next_u32(1);
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
    fn test_string_values() {
        let instructions = vec![
            SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::StringValue("shoveler".to_string()),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(1),
                rhs: Symbol::BoolValue(false),
            },
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(15),
                rhs: Symbol::StringValue("canvasback".to_string()),
                label: 1,
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(2),
                rhs: Symbol::StringValue("canvasback".to_string()),
            },
            SymbolicInstruction::Label(1),
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("bufflehead".to_string(), ValueType::Number),
                rhs: Symbol::NumberValue(1),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("pintail".to_string(), ValueType::Enum),
                rhs: Symbol::EnumValue("mallard".to_string()),
            },
        ];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header();
        checker.verify_sym_table_header(67);

        checker.verify_symbol_table(&[
            "shoveler",
            "canvasback",
            "bufflehead",
            "pintail",
            "mallard",
        ]);

        checker.verify_instructions_header((COND_ABORT_BYTES * 5) + COND_JMP_BYTES + JMP_PAD_BYTES);
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 0 },
        );
        checker.verify_jmp_if_equal(
            COND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 2 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        checker.verify_jmp_pad();
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 3 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 4 },
            EncodedValue { value_type: RawValueType::EnumValue, value: 5 },
        );
        checker.verify_end();
    }

    #[test]
    fn test_empty_symbol_table() {
        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(vec![SymbolicInstruction::UnconditionalAbort]),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());

        checker.verify_bind_rules_header();
        checker.verify_sym_table_header(0);
        checker.verify_instructions_header(UNCOND_ABORT_BYTES);
        checker.verify_unconditional_abort();
        checker.verify_end();
    }

    #[test]
    fn test_duplicate_symbols() {
        let instructions = vec![
            SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::StringValue("puffleg".to_string()),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("sunangel".to_string(), ValueType::Number),
                rhs: Symbol::NumberValue(1),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("puffleg".to_string(), ValueType::Number),
                rhs: Symbol::NumberValue(1),
            },
            SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::StringValue("sunangel".to_string()),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("puffleg".to_string(), ValueType::Str),
                rhs: Symbol::StringValue("sunangel".to_string()),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("mountaingem".to_string(), ValueType::Str),
                rhs: Symbol::StringValue("mountaingem".to_string()),
            },
        ];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header();
        checker.verify_sym_table_header(41);

        checker.verify_symbol_table(&["puffleg", "sunangel", "mountaingem"]);

        checker.verify_instructions_header(COND_ABORT_BYTES * 6);
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 2 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 1 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 3 },
            EncodedValue { value_type: RawValueType::StringValue, value: 3 },
        );
    }

    #[test]
    fn test_long_string() {
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

        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::DeprecatedKey(5),
            rhs: Symbol::StringValue(long_str.to_string()),
        }];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        assert_eq!(
            Err(BindRulesEncodeError::InvalidStringLength(long_str.to_string())),
            encode_to_bytecode_v2(bind_rules)
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

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header();
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
    fn test_unconditional_jump_statement() {
        let instructions = vec![
            SymbolicInstruction::UnconditionalJump { label: 1 },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header();
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

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header();
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

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header();
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

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());

        checker.verify_bind_rules_header();
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

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header();
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

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header();
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

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        assert_eq!(Err(BindRulesEncodeError::DuplicateLabel(1)), encode_to_bytecode_v2(bind_rules));
    }

    #[test]
    fn test_unused_label() {
        let instructions = vec![
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
            SymbolicInstruction::UnconditionalAbort,
        ];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());

        checker.verify_bind_rules_header();
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

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        assert_eq!(
            Err(BindRulesEncodeError::InvalidGotoLocation(1)),
            encode_to_bytecode_v2(bind_rules)
        );
    }

    #[test]
    fn test_missing_label() {
        let instructions = vec![
            SymbolicInstruction::UnconditionalJump { label: 2 },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        assert_eq!(Err(BindRulesEncodeError::MissingLabel(2)), encode_to_bytecode_v2(bind_rules));
    }

    #[test]
    fn test_mismatch_value_types() {
        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::Key("waxwing".to_string(), ValueType::Number),
            rhs: Symbol::BoolValue(true),
        }];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        assert_eq!(
            Err(BindRulesEncodeError::MismatchValueTypes(ValueType::Number, ValueType::Bool)),
            encode_to_bytecode_v2(bind_rules)
        );
    }

    #[test]
    fn test_invalid_lhs_symbol() {
        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::NumberValue(5),
            rhs: Symbol::BoolValue(true),
        }];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        assert_eq!(
            Err(BindRulesEncodeError::IncorrectTypesInValueComparison),
            encode_to_bytecode_v2(bind_rules)
        );
    }

    #[test]
    fn test_invalid_rhs_symbol() {
        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::DeprecatedKey(5),
            rhs: Symbol::DeprecatedKey(6),
        }];
        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };
        assert_eq!(
            Err(BindRulesEncodeError::IncorrectTypesInValueComparison),
            encode_to_bytecode_v2(bind_rules)
        );

        let instructions = vec![SymbolicInstruction::AbortIfNotEqual {
            lhs: Symbol::DeprecatedKey(5),
            rhs: Symbol::Key("wagtail".to_string(), ValueType::Number),
        }];
        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };
        assert_eq!(
            Err(BindRulesEncodeError::IncorrectTypesInValueComparison),
            encode_to_bytecode_v2(bind_rules)
        );
    }

    #[test]
    fn test_missing_match_instruction() {
        let instructions =
            vec![SymbolicInstruction::UnconditionalAbort, SymbolicInstruction::UnconditionalBind];

        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(instructions),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };

        assert_eq!(Err(BindRulesEncodeError::MatchNotSupported), encode_to_bytecode_v2(bind_rules));
    }

    #[test]
    fn test_composite() {
        let primary_node = vec![
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(1),
                rhs: Symbol::BoolValue(false),
            },
            SymbolicInstruction::UnconditionalAbort,
        ];

        let additional_node_instructions = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(15),
                rhs: Symbol::StringValue("ruff".to_string()),
                label: 1,
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("plover".to_string(), ValueType::Number),
                rhs: Symbol::NumberValue(1),
            },
            SymbolicInstruction::Label(1),
            SymbolicInstruction::UnconditionalAbort,
        ];

        let bind_rules = CompositeBindRules {
            symbol_table: HashMap::new(),
            device_name: "wader".to_string(),
            primary_node_instructions: to_symbolic_inst_info(primary_node),
            additional_node_instructions: vec![to_symbolic_inst_info(additional_node_instructions)],
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header();
        checker.verify_sym_table_header(30);

        checker.verify_symbol_table(&["wader", "ruff", "plover"]);

        let primary_node_bytes = COND_ABORT_BYTES + UNCOND_ABORT_BYTES;
        let additional_node_bytes =
            COND_JMP_BYTES + COND_ABORT_BYTES + JMP_PAD_BYTES + UNCOND_ABORT_BYTES;
        checker.verify_composite_header(
            (NODE_HEADER_BYTES * 2)
                + COMPOSITE_NAME_ID_BYTES
                + primary_node_bytes
                + additional_node_bytes,
        );

        // Verify primary node.
        checker.verify_node_type(RawNodeType::Primary, primary_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 0 },
        );
        checker.verify_unconditional_abort();

        // Verify additional node.
        checker.verify_node_type(RawNodeType::Additional, additional_node_bytes);
        checker.verify_jmp_if_equal(
            COND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 3 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_jmp_pad();
        checker.verify_unconditional_abort();
        checker.verify_end();
    }

    #[test]
    fn test_composite_sharing_symbols() {
        let primary_node = vec![SymbolicInstruction::AbortIfEqual {
            lhs: Symbol::Key("trembler".to_string(), ValueType::Str),
            rhs: Symbol::StringValue("thrasher".to_string()),
        }];

        let additional_node_instructions = vec![
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("thrasher".to_string(), ValueType::Str),
                rhs: Symbol::StringValue("catbird".to_string()),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("catbird".to_string(), ValueType::Number),
                rhs: Symbol::NumberValue(1),
            },
        ];

        let bind_rules = CompositeBindRules {
            device_name: "mimid".to_string(),
            symbol_table: HashMap::new(),
            primary_node_instructions: to_symbolic_inst_info(primary_node),
            additional_node_instructions: vec![to_symbolic_inst_info(additional_node_instructions)],
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header();

        checker.verify_sym_table_header(48);
        checker.verify_symbol_table(&["mimid", "trembler", "thrasher", "catbird"]);

        let primary_node_bytes = COND_ABORT_BYTES;
        let additional_node_bytes = COND_ABORT_BYTES * 2;
        checker.verify_composite_header(
            (NODE_HEADER_BYTES * 2)
                + COMPOSITE_NAME_ID_BYTES
                + primary_node_bytes
                + additional_node_bytes,
        );

        checker.verify_node_type(RawNodeType::Primary, primary_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 2 },
            EncodedValue { value_type: RawValueType::StringValue, value: 3 },
        );

        checker.verify_node_type(RawNodeType::Additional, additional_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 3 },
            EncodedValue { value_type: RawValueType::StringValue, value: 4 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 4 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_end();
    }

    #[test]
    fn test_composite_same_label_id() {
        let primary_node = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(15),
                rhs: Symbol::NumberValue(5),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let additional_node_instructions = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(10),
                rhs: Symbol::NumberValue(2),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let bind_rules = CompositeBindRules {
            device_name: "currawong".to_string(),
            symbol_table: HashMap::new(),
            primary_node_instructions: to_symbolic_inst_info(primary_node),
            additional_node_instructions: vec![to_symbolic_inst_info(additional_node_instructions)],
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header();

        checker.verify_sym_table_header(14);
        checker.verify_symbol_table(&["currawong"]);

        let primary_node_bytes = COND_JMP_BYTES + UNCOND_ABORT_BYTES + JMP_PAD_BYTES;
        let additional_node_bytes = COND_JMP_BYTES + UNCOND_ABORT_BYTES + JMP_PAD_BYTES;
        checker.verify_composite_header(
            (NODE_HEADER_BYTES * 2)
                + COMPOSITE_NAME_ID_BYTES
                + primary_node_bytes
                + additional_node_bytes,
        );

        checker.verify_node_type(RawNodeType::Primary, primary_node_bytes);
        checker.verify_jmp_if_equal(
            UNCOND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
        );
        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();

        checker.verify_node_type(RawNodeType::Additional, additional_node_bytes);
        checker.verify_jmp_if_equal(
            UNCOND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2 },
        );
        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();

        checker.verify_end();
    }

    #[test]
    fn test_composite_invalid_label() {
        let primary_node = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(15),
                rhs: Symbol::NumberValue(5),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let additional_node_instructions = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(10),
                rhs: Symbol::NumberValue(2),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(2),
        ];

        let bind_rules = CompositeBindRules {
            device_name: "currawong".to_string(),
            symbol_table: HashMap::new(),
            primary_node_instructions: to_symbolic_inst_info(primary_node),
            additional_node_instructions: vec![to_symbolic_inst_info(additional_node_instructions)],
        };

        assert_eq!(
            Err(BindRulesEncodeError::MissingLabel(1)),
            encode_composite_to_bytecode(bind_rules)
        );
    }

    #[test]
    fn test_composite_primary_node_only() {
        let primary_node = vec![SymbolicInstruction::UnconditionalAbort];

        let bind_rules = CompositeBindRules {
            device_name: "treehunter".to_string(),
            symbol_table: HashMap::new(),
            primary_node_instructions: to_symbolic_inst_info(primary_node),
            additional_node_instructions: vec![],
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header();

        checker.verify_sym_table_header(15);
        checker.verify_symbol_table(&["treehunter"]);

        let primary_node_bytes = UNCOND_ABORT_BYTES;
        checker.verify_composite_header(
            NODE_HEADER_BYTES + COMPOSITE_NAME_ID_BYTES + primary_node_bytes,
        );

        checker.verify_node_type(RawNodeType::Primary, primary_node_bytes);
        checker.verify_unconditional_abort();

        checker.verify_end();
    }

    #[test]
    fn test_composite_empty_node_instructions() {
        let bind_rules = CompositeBindRules {
            device_name: "spiderhunter".to_string(),
            symbol_table: HashMap::new(),
            primary_node_instructions: vec![],
            additional_node_instructions: vec![],
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header();

        checker.verify_sym_table_header(17);
        checker.verify_symbol_table(&["spiderhunter"]);

        checker.verify_composite_header(NODE_HEADER_BYTES + COMPOSITE_NAME_ID_BYTES);
        checker.verify_node_type(RawNodeType::Primary, 0);

        checker.verify_end();
    }

    #[test]
    fn test_composite_missing_device_name() {
        let bind_rules = CompositeBindRules {
            device_name: "".to_string(),
            symbol_table: HashMap::new(),
            primary_node_instructions: vec![],
            additional_node_instructions: vec![],
        };

        assert_eq!(
            Err(BindRulesEncodeError::MissingCompositeDeviceName),
            encode_composite_to_bytecode(bind_rules)
        );
    }
}
