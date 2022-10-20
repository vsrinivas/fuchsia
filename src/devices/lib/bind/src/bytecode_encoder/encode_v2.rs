// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bytecode_constants::*;
use crate::bytecode_encoder::error::BindRulesEncodeError;
use crate::bytecode_encoder::instruction_encoder::encode_instructions;
use crate::bytecode_encoder::symbol_table_encoder::SymbolTableEncoder;
use crate::compiler::{BindRules, CompositeBindRules, CompositeNode};
use std::collections::HashSet;

/// Functions for encoding the new bytecode format. When the
/// old bytecode format is deleted, the "v2" should be removed from the names.

pub fn encode_to_bytecode_v2(bind_rules: BindRules) -> Result<Vec<u8>, BindRulesEncodeError> {
    let mut debug_symbol_table_encoder_option =
        if bind_rules.enable_debug { Some(SymbolTableEncoder::new()) } else { None };

    let mut symbol_table_encoder = SymbolTableEncoder::new();
    let mut instruction_bytecode = encode_instructions(
        bind_rules.instructions,
        &mut symbol_table_encoder,
        &mut debug_symbol_table_encoder_option,
    )?;

    let mut bytecode: Vec<u8> = vec![];

    // Encode the header.
    bytecode.extend_from_slice(&BIND_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&BYTECODE_VERSION.to_le_bytes());
    if bind_rules.enable_debug {
        bytecode.push(BYTECODE_ENABLE_DEBUG);
    } else {
        bytecode.push(BYTECODE_DISABLE_DEBUG);
    }

    // Encode the symbol table.
    bytecode.extend_from_slice(&SYMB_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&(symbol_table_encoder.bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut symbol_table_encoder.bytecode);

    // Encode the instruction section.
    bytecode.extend_from_slice(&INSTRUCTION_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&(instruction_bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut instruction_bytecode);

    // Encode the debug section when enable_debug flag is true.
    if let Some(mut debug_symbol_table_encoder) = debug_symbol_table_encoder_option {
        encode_debug_section(&mut bytecode, &mut debug_symbol_table_encoder)?;
    }

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

fn encode_debug_section(
    bytecode: &mut Vec<u8>,
    debug_symbol_table_encoder: &mut SymbolTableEncoder,
) -> Result<(), BindRulesEncodeError> {
    // Debug information header
    bytecode.extend_from_slice(&DEBG_MAGIC_NUM.to_be_bytes());
    if debug_symbol_table_encoder.bytecode.len() == 0 {
        bytecode
            .extend_from_slice(&(debug_symbol_table_encoder.bytecode.len() as u32).to_le_bytes());
    } else {
        // Extend the debug information section to include the
        // length of the debug symbol table bytecode and header
        bytecode.extend_from_slice(
            &((debug_symbol_table_encoder.bytecode.len() + HEADER_SZ) as u32).to_le_bytes(),
        );
        // Debug symbol table section
        bytecode.extend_from_slice(&DBSY_MAGIC_NUM.to_be_bytes());
        bytecode
            .extend_from_slice(&(debug_symbol_table_encoder.bytecode.len() as u32).to_le_bytes());
        bytecode.append(&mut debug_symbol_table_encoder.bytecode);
    }

    Ok(())
}

fn append_composite_node(
    bytecode: &mut Vec<u8>,
    node: CompositeNode,
    node_type: RawNodeType,
    symbol_table_encoder: &mut SymbolTableEncoder,
    debug_symbol_table_encoder: &mut Option<SymbolTableEncoder>,
) -> Result<(), BindRulesEncodeError> {
    if node.name.is_empty() {
        return Err(BindRulesEncodeError::MissingCompositeNodeName);
    }

    bytecode.push(node_type as u8);
    bytecode.extend_from_slice(&symbol_table_encoder.get_key(node.name)?.to_le_bytes());

    let mut inst_bytecode =
        encode_instructions(node.instructions, symbol_table_encoder, debug_symbol_table_encoder)?;
    bytecode.extend_from_slice(&(inst_bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut inst_bytecode);
    Ok(())
}

pub fn encode_composite_to_bytecode(
    bind_rules: CompositeBindRules,
) -> Result<Vec<u8>, BindRulesEncodeError> {
    let mut symbol_table_encoder = SymbolTableEncoder::new();
    let mut debug_symbol_table_encoder_option =
        if bind_rules.enable_debug { Some(SymbolTableEncoder::new()) } else { None };

    if bind_rules.device_name.is_empty() {
        return Err(BindRulesEncodeError::MissingCompositeDeviceName);
    }

    // Instruction bytecode begins with the device name ID.
    let device_name_id = symbol_table_encoder.get_key(bind_rules.device_name)?;
    let mut inst_bytecode = device_name_id.to_le_bytes().to_vec();

    let mut node_names = HashSet::new();

    // Add instructions from the primary node.
    node_names.insert(bind_rules.primary_node.name.clone());
    append_composite_node(
        &mut inst_bytecode,
        bind_rules.primary_node,
        RawNodeType::Primary,
        &mut symbol_table_encoder,
        &mut debug_symbol_table_encoder_option,
    )?;

    // Add instructions from additional nodes.
    for node in bind_rules.additional_nodes.into_iter() {
        if node_names.contains(&node.name) {
            return Err(BindRulesEncodeError::DuplicateCompositeNodeName(node.name));
        }
        node_names.insert(node.name.clone());
        append_composite_node(
            &mut inst_bytecode,
            node,
            RawNodeType::Additional,
            &mut symbol_table_encoder,
            &mut debug_symbol_table_encoder_option,
        )?;
    }

    // Add instructions from optional nodes.
    for node in bind_rules.optional_nodes.into_iter() {
        if node_names.contains(&node.name) {
            return Err(BindRulesEncodeError::DuplicateCompositeNodeName(node.name));
        }
        node_names.insert(node.name.clone());
        append_composite_node(
            &mut inst_bytecode,
            node,
            RawNodeType::Optional,
            &mut symbol_table_encoder,
            &mut debug_symbol_table_encoder_option,
        )?;
    }

    // Put all of the sections together.
    let mut bytecode: Vec<u8> = vec![];

    // Encode the header.
    bytecode.extend_from_slice(&BIND_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&BYTECODE_VERSION.to_le_bytes());
    if bind_rules.enable_debug {
        bytecode.push(BYTECODE_ENABLE_DEBUG);
    } else {
        bytecode.push(BYTECODE_DISABLE_DEBUG);
    }

    // Encode the symbol table.
    bytecode.extend_from_slice(&SYMB_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&(symbol_table_encoder.bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut symbol_table_encoder.bytecode);

    // Encode the instruction section.
    bytecode.extend_from_slice(&COMPOSITE_MAGIC_NUM.to_be_bytes());
    bytecode.extend_from_slice(&(inst_bytecode.len() as u32).to_le_bytes());
    bytecode.append(&mut inst_bytecode);

    // Encode the debug section when enable_debug flag is true.
    if let Some(mut debug_symbol_table_encoder) = debug_symbol_table_encoder_option {
        encode_debug_section(&mut bytecode, &mut debug_symbol_table_encoder)?;
    }

    Ok(bytecode)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bytecode_encoder::bytecode_checker::*;
    use crate::compiler::Symbol;
    use crate::compiler::{SymbolicInstruction, SymbolicInstructionInfo};
    use crate::parser::bind_library::ValueType;
    use std::collections::HashMap;

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

    fn composite_node<'a>(
        name: String,
        instructions: Vec<SymbolicInstruction>,
    ) -> CompositeNode<'a> {
        CompositeNode { name: name, instructions: to_symbolic_inst_info(instructions) }
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());

        checker.verify_bind_rules_header(false);
        checker.verify_sym_table_header(0);
        checker.verify_instructions_header(UNCOND_ABORT_BYTES);
        checker.verify_unconditional_abort();
        checker.verify_end();
    }

    #[test]
    fn test_enable_debug_flag_missing_ast_location() {
        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(vec![SymbolicInstruction::UnconditionalAbort]),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: true,
        };

        assert_eq!(
            Err(BindRulesEncodeError::MissingAstLocation),
            encode_to_bytecode_v2(bind_rules)
        );
    }

    #[test]
    fn test_enable_debug_flag_false() {
        let bind_rules = BindRules {
            instructions: to_symbolic_inst_info(vec![SymbolicInstruction::UnconditionalAbort]),
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());

        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());

        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
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
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_to_bytecode_v2(bind_rules).unwrap());

        checker.verify_bind_rules_header(false);
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
            enable_debug: false,
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
            enable_debug: false,
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
            enable_debug: false,
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
            enable_debug: false,
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
            enable_debug: false,
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
            enable_debug: false,
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
            enable_debug: false,
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

        let additional_nodes = vec![
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
            primary_node: composite_node("stilt".to_string(), primary_node),
            additional_nodes: vec![composite_node("avocet".to_string(), additional_nodes)],
            optional_nodes: vec![],
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
        checker.verify_sym_table_header(51);

        checker.verify_symbol_table(&["wader", "stilt", "avocet", "ruff", "plover"]);

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
        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 0 },
        );
        checker.verify_unconditional_abort();

        // Verify additional node.
        checker.verify_node_header(RawNodeType::Additional, 3, additional_node_bytes);
        checker.verify_jmp_if_equal(
            COND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::StringValue, value: 4 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 5 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_jmp_pad();
        checker.verify_unconditional_abort();
        checker.verify_end();
    }

    #[test]
    fn test_composite_optional() {
        let primary_node_inst = vec![
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(1),
                rhs: Symbol::BoolValue(false),
            },
            SymbolicInstruction::UnconditionalAbort,
        ];

        let optional_node_inst = vec![
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
            primary_node: composite_node("stilt".to_string(), primary_node_inst),
            additional_nodes: vec![],
            optional_nodes: vec![composite_node("avocet".to_string(), optional_node_inst)],
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
        checker.verify_sym_table_header(51);

        checker.verify_symbol_table(&["wader", "stilt", "avocet", "ruff", "plover"]);

        let primary_node_bytes = COND_ABORT_BYTES + UNCOND_ABORT_BYTES;
        let optional_node_bytes =
            COND_JMP_BYTES + COND_ABORT_BYTES + JMP_PAD_BYTES + UNCOND_ABORT_BYTES;
        checker.verify_composite_header(
            (NODE_HEADER_BYTES * 2)
                + COMPOSITE_NAME_ID_BYTES
                + primary_node_bytes
                + optional_node_bytes,
        );

        // Verify primary node.
        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 0 },
        );
        checker.verify_unconditional_abort();

        // Verify optional node.
        checker.verify_node_header(RawNodeType::Optional, 3, optional_node_bytes);
        checker.verify_jmp_if_equal(
            COND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::StringValue, value: 4 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 5 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_jmp_pad();
        checker.verify_unconditional_abort();
        checker.verify_end();
    }

    #[test]
    fn test_composite_additional_and_optional() {
        let primary_node_inst = vec![
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(1),
                rhs: Symbol::BoolValue(false),
            },
            SymbolicInstruction::UnconditionalAbort,
        ];

        let additional_node_inst = vec![
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("thrasher".to_string(), ValueType::Str),
                rhs: Symbol::StringValue("catbird".to_string()),
            },
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::Key("catbird".to_string(), ValueType::Number),
                rhs: Symbol::NumberValue(1),
            },
        ];

        let optional_node_inst = vec![
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
            primary_node: composite_node("stilt".to_string(), primary_node_inst),
            additional_nodes: vec![composite_node("avocet".to_string(), additional_node_inst)],
            optional_nodes: vec![composite_node("mockingbird".to_string(), optional_node_inst)],
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);
        checker.verify_sym_table_header(92);

        checker.verify_symbol_table(&[
            "wader",
            "stilt",
            "avocet",
            "thrasher",
            "catbird",
            "mockingbird",
            "ruff",
            "plover",
        ]);

        let primary_node_bytes = COND_ABORT_BYTES + UNCOND_ABORT_BYTES;
        let additional_node_bytes = COND_ABORT_BYTES * 2;
        let optional_node_bytes =
            COND_JMP_BYTES + COND_ABORT_BYTES + JMP_PAD_BYTES + UNCOND_ABORT_BYTES;
        checker.verify_composite_header(
            (NODE_HEADER_BYTES * 3)
                + COMPOSITE_NAME_ID_BYTES
                + primary_node_bytes
                + additional_node_bytes
                + optional_node_bytes,
        );

        // Verify primary node.
        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 0 },
        );
        checker.verify_unconditional_abort();

        // Verify additional node.
        checker.verify_node_header(RawNodeType::Additional, 3, additional_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 4 },
            EncodedValue { value_type: RawValueType::StringValue, value: 5 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 5 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );

        // Verify optional node.
        checker.verify_node_header(RawNodeType::Optional, 6, optional_node_bytes);
        checker.verify_jmp_if_equal(
            COND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::StringValue, value: 7 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 8 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_jmp_pad();
        checker.verify_unconditional_abort();
        checker.verify_end();
    }

    #[test]
    fn test_composite_enable_debug_missing_ast_location() {
        let primary_node = vec![
            SymbolicInstruction::AbortIfEqual {
                lhs: Symbol::DeprecatedKey(1),
                rhs: Symbol::BoolValue(false),
            },
            SymbolicInstruction::UnconditionalAbort,
        ];

        let additional_nodes = vec![
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
            primary_node: composite_node("stilt".to_string(), primary_node),
            additional_nodes: vec![composite_node("avocet".to_string(), additional_nodes)],
            optional_nodes: vec![],
            enable_debug: true,
        };

        assert_eq!(
            Err(BindRulesEncodeError::MissingAstLocation),
            encode_composite_to_bytecode(bind_rules)
        );
    }

    #[test]
    fn test_composite_sharing_symbols() {
        let primary_node_inst = vec![SymbolicInstruction::AbortIfEqual {
            lhs: Symbol::Key("trembler".to_string(), ValueType::Str),
            rhs: Symbol::StringValue("thrasher".to_string()),
        }];

        let additional_node_inst = vec![
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
            primary_node: composite_node("catbird".to_string(), primary_node_inst),
            additional_nodes: vec![composite_node("mockingbird".to_string(), additional_node_inst)],
            optional_nodes: vec![],
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);

        checker.verify_sym_table_header(64);
        checker.verify_symbol_table(&["mimid", "catbird", "trembler", "thrasher", "mockingbird"]);

        let primary_node_bytes = COND_ABORT_BYTES;
        let additional_node_bytes = COND_ABORT_BYTES * 2;
        checker.verify_composite_header(
            (NODE_HEADER_BYTES * 2)
                + COMPOSITE_NAME_ID_BYTES
                + primary_node_bytes
                + additional_node_bytes,
        );

        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 3 },
            EncodedValue { value_type: RawValueType::StringValue, value: 4 },
        );

        checker.verify_node_header(RawNodeType::Additional, 5, additional_node_bytes);
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 4 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        checker.verify_abort_equal(
            EncodedValue { value_type: RawValueType::Key, value: 2 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );
        checker.verify_end();
    }

    #[test]
    fn test_composite_same_label_id() {
        let primary_node_inst = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(15),
                rhs: Symbol::NumberValue(5),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let additional_node_inst = vec![
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
            primary_node: composite_node("butcherbird".to_string(), primary_node_inst),
            additional_nodes: vec![composite_node("bushshrike".to_string(), additional_node_inst)],
            optional_nodes: vec![],
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);

        checker.verify_sym_table_header(45);
        checker.verify_symbol_table(&["currawong", "butcherbird", "bushshrike"]);

        let primary_node_bytes = COND_JMP_BYTES + UNCOND_ABORT_BYTES + JMP_PAD_BYTES;
        let additional_node_bytes = COND_JMP_BYTES + UNCOND_ABORT_BYTES + JMP_PAD_BYTES;
        checker.verify_composite_header(
            (NODE_HEADER_BYTES * 2)
                + COMPOSITE_NAME_ID_BYTES
                + primary_node_bytes
                + additional_node_bytes,
        );

        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_jmp_if_equal(
            UNCOND_ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
        );
        checker.verify_unconditional_abort();
        checker.verify_jmp_pad();

        checker.verify_node_header(RawNodeType::Additional, 3, additional_node_bytes);
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
        let primary_node_inst = vec![
            SymbolicInstruction::JumpIfEqual {
                lhs: Symbol::DeprecatedKey(15),
                rhs: Symbol::NumberValue(5),
                label: 1,
            },
            SymbolicInstruction::UnconditionalAbort,
            SymbolicInstruction::Label(1),
        ];

        let additional_node_inst = vec![
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
            primary_node: composite_node("butcherbird".to_string(), primary_node_inst),
            additional_nodes: vec![composite_node("bushshrike".to_string(), additional_node_inst)],
            optional_nodes: vec![],
            enable_debug: false,
        };

        assert_eq!(
            Err(BindRulesEncodeError::MissingLabel(1)),
            encode_composite_to_bytecode(bind_rules)
        );
    }

    #[test]
    fn test_composite_primary_node_only() {
        let primary_node_inst = vec![SymbolicInstruction::UnconditionalAbort];

        let bind_rules = CompositeBindRules {
            device_name: "treehunter".to_string(),
            symbol_table: HashMap::new(),
            additional_nodes: vec![],
            optional_nodes: vec![],
            primary_node: composite_node("bananaquit".to_string(), primary_node_inst),
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);

        checker.verify_sym_table_header(30);
        checker.verify_symbol_table(&["treehunter", "bananaquit"]);

        let primary_node_bytes = UNCOND_ABORT_BYTES;
        checker.verify_composite_header(
            NODE_HEADER_BYTES + COMPOSITE_NAME_ID_BYTES + primary_node_bytes,
        );

        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_unconditional_abort();

        checker.verify_end();
    }

    #[test]
    fn test_composite_empty_node_instructions() {
        let bind_rules = CompositeBindRules {
            device_name: "spiderhunter".to_string(),
            symbol_table: HashMap::new(),
            primary_node: composite_node("sunbird".to_string(), vec![]),
            additional_nodes: vec![],
            optional_nodes: vec![],
            enable_debug: false,
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header(false);

        checker.verify_sym_table_header(29);
        checker.verify_symbol_table(&["spiderhunter", "sunbird"]);

        checker.verify_composite_header(NODE_HEADER_BYTES + COMPOSITE_NAME_ID_BYTES);
        checker.verify_node_header(RawNodeType::Primary, 2, 0);

        checker.verify_end();
    }

    #[test]
    fn test_composite_missing_device_name() {
        let bind_rules = CompositeBindRules {
            device_name: "".to_string(),
            symbol_table: HashMap::new(),
            primary_node: composite_node("pewee".to_string(), vec![]),
            additional_nodes: vec![],
            optional_nodes: vec![],
            enable_debug: false,
        };

        assert_eq!(
            Err(BindRulesEncodeError::MissingCompositeDeviceName),
            encode_composite_to_bytecode(bind_rules)
        );
    }

    #[test]
    fn test_composite_missing_node_name() {
        let bind_rules = CompositeBindRules {
            device_name: "pewee".to_string(),
            symbol_table: HashMap::new(),
            primary_node: composite_node("".to_string(), vec![]),
            additional_nodes: vec![],
            optional_nodes: vec![],
            enable_debug: false,
        };

        assert_eq!(
            Err(BindRulesEncodeError::MissingCompositeNodeName),
            encode_composite_to_bytecode(bind_rules)
        );
    }

    #[test]
    fn test_duplicate_nodes() {
        let bind_rules = CompositeBindRules {
            device_name: "flycatcher".to_string(),
            symbol_table: HashMap::new(),
            primary_node: composite_node("pewee".to_string(), vec![]),
            additional_nodes: vec![composite_node("pewee".to_string(), vec![])],
            optional_nodes: vec![],
            enable_debug: false,
        };

        assert_eq!(
            Err(BindRulesEncodeError::DuplicateCompositeNodeName("pewee".to_string())),
            encode_composite_to_bytecode(bind_rules)
        );

        let bind_rules = CompositeBindRules {
            device_name: "flycatcher".to_string(),
            symbol_table: HashMap::new(),
            primary_node: composite_node("pewee".to_string(), vec![]),
            additional_nodes: vec![
                composite_node("phoebe".to_string(), vec![]),
                composite_node("kingbird".to_string(), vec![]),
                composite_node("phoebe".to_string(), vec![]),
            ],
            optional_nodes: vec![],
            enable_debug: false,
        };

        assert_eq!(
            Err(BindRulesEncodeError::DuplicateCompositeNodeName("phoebe".to_string())),
            encode_composite_to_bytecode(bind_rules)
        );
    }
}
