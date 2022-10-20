// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bytecode_encoder::error::BindRulesEncodeError;
use crate::bytecode_encoder::symbol_table_encoder::SymbolTableEncoder;
use crate::compiler::Symbol;
use crate::debugger::offline_debugger::AstLocation;
use crate::parser::bind_rules::{Condition, Statement};
use crate::parser::common::Value;

pub fn add_astlocation_enum_debug_symbols(
    location: &AstLocation,
    debug_symbol_table_encoder: &mut SymbolTableEncoder,
) -> Result<(), BindRulesEncodeError> {
    // Go through each value in the ASTLocation enum to add the CompoundIdentifier
    // or String Literal to the debug symbol table
    match location {
        AstLocation::ConditionStatement(statement) => {
            add_statement_enum_debug_symbols(statement, debug_symbol_table_encoder)?;
        }
        AstLocation::AcceptStatementValue { identifier, value, span: _ } => {
            debug_symbol_table_encoder.get_key(identifier.to_string())?;
            add_value_enum_debug_symbols(value, debug_symbol_table_encoder)?;
        }
        AstLocation::AcceptStatementFailure { identifier, symbol, span: _ } => {
            debug_symbol_table_encoder.get_key(identifier.to_string())?;
            add_symbol_enum_debug_symbols(symbol, debug_symbol_table_encoder)?;
        }
        _ => {}
    }
    Ok(())
}

pub fn add_symbol_enum_debug_symbols(
    symbol: &Symbol,
    debug_symbol_table_encoder: &mut SymbolTableEncoder,
) -> Result<(), BindRulesEncodeError> {
    // Go through each value in the Symbol enum to add the CompoundIdentifier
    // or String Literal to the debug symbol table
    match symbol {
        Symbol::Key(value, _) => {
            debug_symbol_table_encoder.get_key(value.to_string())?;
        }
        Symbol::StringValue(val) => {
            debug_symbol_table_encoder.get_key(val.to_string())?;
        }
        Symbol::EnumValue(val) => {
            debug_symbol_table_encoder.get_key(val.to_string())?;
        }
        _ => {}
    }
    Ok(())
}

pub fn add_statement_enum_debug_symbols(
    statement: &Statement,
    debug_symbol_table_encoder: &mut SymbolTableEncoder,
) -> Result<(), BindRulesEncodeError> {
    // Go through each value in the Statement enum to add the CompoundIdentifier
    // or String Literal to the debug symbol table
    match statement {
        Statement::ConditionStatement { span: _, condition } => {
            add_condition_enum_debug_symbols(condition, debug_symbol_table_encoder)?;
        }
        Statement::Accept { span: _, identifier, values } => {
            debug_symbol_table_encoder.get_key(identifier.to_string())?;
            // Go through each value in the Value vector to to add the CompoundIdentifier
            // or String Literal to the debug symbol table
            for val in values {
                add_value_enum_debug_symbols(val, debug_symbol_table_encoder)?;
            }
        }
        Statement::If { span: _, blocks, else_block } => {
            for (condition, statement_vector) in blocks {
                // Go through each Condition and Statement to add the CompoundIdentifier
                // or String Literal to the debug symbol table
                add_condition_enum_debug_symbols(condition, debug_symbol_table_encoder)?;
                for statement in statement_vector {
                    add_statement_enum_debug_symbols(statement, debug_symbol_table_encoder)?;
                }
            }
            for statement in else_block {
                add_statement_enum_debug_symbols(statement, debug_symbol_table_encoder)?;
            }
        }
        _ => {}
    }
    Ok(())
}

pub fn add_condition_enum_debug_symbols(
    condition: &Condition,
    debug_symbol_table_encoder: &mut SymbolTableEncoder,
) -> Result<(), BindRulesEncodeError> {
    // Go through the lhs and rhs variables in the Conidtion enum to
    // add the CompoundIdentifier or String Literal to the debug symbol table
    debug_symbol_table_encoder.get_key(condition.lhs.to_string())?;
    add_value_enum_debug_symbols(&condition.rhs, debug_symbol_table_encoder)?;
    Ok(())
}

pub fn add_value_enum_debug_symbols(
    value: &Value,
    debug_symbol_table_encoder: &mut SymbolTableEncoder,
) -> Result<(), BindRulesEncodeError> {
    // Go through each variable in the Value enum to to add the CompoundIdentifier
    // or String Literal to the debug symbol table
    match value {
        Value::StringLiteral(val) => {
            debug_symbol_table_encoder.get_key(val.to_string())?;
        }
        Value::Identifier(val) => {
            debug_symbol_table_encoder.get_key(val.to_string())?;
        }
        _ => {}
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bytecode_constants::*;
    use crate::bytecode_encoder::bytecode_checker::*;
    use crate::bytecode_encoder::encode_v2::encode_composite_to_bytecode;
    use crate::bytecode_encoder::encode_v2::encode_to_bytecode_v2;
    use crate::compiler::compiler::compile;
    use crate::compiler::CompositeBindRules;
    use crate::compiler::CompositeNode;
    use crate::compiler::{BindRules, CompiledBindRules};
    use crate::compiler::{Symbol, SymbolicInstruction, SymbolicInstructionInfo};
    use crate::make_identifier;
    use crate::parser::bind_library::ValueType;
    use crate::parser::bind_rules::ConditionOp;
    use crate::parser::common::{CompoundIdentifier, Span};
    use assert_matches::assert_matches;
    use std::collections::HashMap;

    #[test]
    fn test_encode_debug_symbol_add_condition_symbols() {
        // AST Location for "fuchsia.BIND_PROTOCOL == shoveler".
        let location = AstLocation::ConditionStatement(Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!["fuchsia.BIND_PROTOCOL"],
                op: ConditionOp::Equals,
                rhs: Value::StringLiteral("shoveler".to_string()),
            },
        });

        let instruct_info = vec![SymbolicInstructionInfo {
            location: Some(location),
            instruction: SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::DeprecatedKey(5),
                rhs: Symbol::StringValue("shoveler".to_string()),
            },
        }];

        let bind_rules = BindRules {
            instructions: instruct_info,
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: true,
        };

        let bytecode = encode_to_bytecode_v2(bind_rules).unwrap();
        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(true);

        // Symbol table section.
        checker.verify_sym_table_header(13);
        checker.verify_symbol_table(&["shoveler"]);

        // Instruction section.
        checker.verify_instructions_header(COND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES);
        checker.verify_debug_line_number(1);
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );

        // Debug section.
        checker.verify_debug_header(47);
        checker.verify_debug_symbol_table_header(39);
        checker.verify_symbol_table(&["fuchsia.BIND_PROTOCOL", "shoveler"]);

        checker.verify_end();
    }

    #[test]
    fn test_encode_debug_symbol_ast_accept_statement_val() {
        // AST Location for "fuchsia.BIND_PROTOCOL" and "rail".
        let location = AstLocation::AcceptStatementValue {
            identifier: make_identifier!["fuchsia.BIND_USB_CLASS"],
            value: Value::Identifier(make_identifier!["rail"]),
            span: Span::new(),
        };

        let instruct_info = vec![SymbolicInstructionInfo {
            location: Some(location),
            instruction: SymbolicInstruction::UnconditionalAbort,
        }];

        let bind_rules = BindRules {
            instructions: instruct_info,
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: true,
        };

        let bytecode = encode_to_bytecode_v2(bind_rules).unwrap();
        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(true);
        checker.verify_sym_table_header(0);

        // Instruction section.
        checker.verify_instructions_header(UNCOND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES);
        checker.verify_debug_line_number(1);
        checker.verify_unconditional_abort();

        // Debug section.
        checker.verify_debug_header(44);
        checker.verify_debug_symbol_table_header(36);
        checker.verify_symbol_table(&["fuchsia.BIND_USB_CLASS", "rail"]);
        checker.verify_end();
    }

    #[test]
    fn test_encode_debug_symbol_ast_accept_statement_failure() {
        // AST Location for "fuchsia.BIND_USB_CLASS" and "wagon".
        let location1 = AstLocation::AcceptStatementFailure {
            identifier: make_identifier!["fuchsia.BIND_USB_CLASS"],
            symbol: Symbol::Key("wagon".to_string(), ValueType::Str),
            span: Span::new(),
        };

        // AST Location for "fuchsia.BIND_PROTOCOL" and "dog".
        let location2 = AstLocation::AcceptStatementFailure {
            identifier: make_identifier!["fuchsia.BIND_PROTOCOL"],
            symbol: Symbol::StringValue("dog".to_string()),
            span: Span::new(),
        };

        // AST Location for "fuchsia.BIND_USB_VID" and "flower".
        let location3 = AstLocation::AcceptStatementFailure {
            identifier: make_identifier!["fuchsia.BIND_USB_VID"],
            symbol: Symbol::EnumValue("flower".to_string()),
            span: Span::new(),
        };

        let instruct_info = vec![
            SymbolicInstructionInfo {
                location: Some(location1),
                instruction: SymbolicInstruction::UnconditionalAbort,
            },
            SymbolicInstructionInfo {
                location: Some(location2),
                instruction: SymbolicInstruction::UnconditionalAbort,
            },
            SymbolicInstructionInfo {
                location: Some(location3),
                instruction: SymbolicInstruction::UnconditionalAbort,
            },
        ];

        let bind_rules = BindRules {
            instructions: instruct_info,
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: true,
        };

        let bytecode = encode_to_bytecode_v2(bind_rules).unwrap();
        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(true);
        checker.verify_sym_table_header(0);

        // Instruction section.
        checker.verify_instructions_header((UNCOND_ABORT_BYTES * 3) + (DEBG_LINE_NUMBER_BYTES * 3));
        checker.verify_debug_line_number(1);
        checker.verify_unconditional_abort();

        checker.verify_debug_line_number(1);
        checker.verify_unconditional_abort();

        checker.verify_debug_line_number(1);
        checker.verify_unconditional_abort();

        // Debug section.
        checker.verify_debug_header(115);
        checker.verify_debug_symbol_table_header(107);
        checker.verify_symbol_table(&[
            "fuchsia.BIND_USB_CLASS",
            "wagon",
            "fuchsia.BIND_PROTOCOL",
            "dog",
            "fuchsia.BIND_USB_VID",
            "flower",
        ]);
        checker.verify_end();
    }

    #[test]
    fn test_encode_debug_symbol_statement_accept() {
        // AST Location for "fuchsia.BIND_USB_CLASS", "apple", and "orange".
        let location = AstLocation::ConditionStatement(Statement::Accept {
            span: Span::new(),
            identifier: make_identifier!["fuchsia.BIND_USB_CLASS"],
            values: vec![
                Value::NumericLiteral(1),
                Value::StringLiteral("apple".to_string()),
                Value::Identifier(make_identifier!["orange"]),
            ],
        });

        let instruct_info = vec![SymbolicInstructionInfo {
            location: Some(location),
            instruction: SymbolicInstruction::UnconditionalAbort,
        }];

        let bind_rules = BindRules {
            instructions: instruct_info,
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: true,
        };

        let bytecode = encode_to_bytecode_v2(bind_rules).unwrap();
        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(true);
        checker.verify_sym_table_header(0);

        // Instruction section.
        checker.verify_instructions_header(UNCOND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES);
        checker.verify_debug_line_number(1);
        checker.verify_unconditional_abort();

        // Debug section.
        checker.verify_debug_header(56);
        checker.verify_debug_symbol_table_header(48);
        checker.verify_symbol_table(&["fuchsia.BIND_USB_CLASS", "apple", "orange"]);
        checker.verify_end();
    }

    #[test]
    fn test_encode_debug_symbol_statement_if() {
        // AST Location for "fuchsia.BIND_PROTOCOL, "wagon",
        // "fuchsia.BIND_USB_CLASS", flower, and "fuchsia.BIND_USB_VID".
        let location = AstLocation::ConditionStatement(Statement::If {
            span: Span::new(),
            blocks: vec![(
                Condition {
                    span: Span::new(),
                    lhs: make_identifier!["fuchsia.BIND_PROTOCOL"],
                    op: ConditionOp::Equals,
                    rhs: Value::Identifier(make_identifier!["wagon"]),
                },
                vec![Statement::ConditionStatement {
                    span: Span::new(),
                    condition: Condition {
                        span: Span::new(),
                        lhs: make_identifier!["fuchsia.BIND_USB_CLASS"],
                        op: ConditionOp::Equals,
                        rhs: Value::StringLiteral("flower".to_string()),
                    },
                }],
            )],
            else_block: vec![Statement::ConditionStatement {
                span: Span::new(),
                condition: Condition {
                    span: Span::new(),
                    lhs: make_identifier!["fuchsia.BIND_USB_VID"],
                    op: ConditionOp::Equals,
                    rhs: Value::NumericLiteral(2),
                },
            }],
        });

        let instruct_info = vec![SymbolicInstructionInfo {
            location: Some(location),
            instruction: SymbolicInstruction::UnconditionalAbort,
        }];

        let bind_rules = BindRules {
            instructions: instruct_info,
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: true,
        };

        let bytecode = encode_to_bytecode_v2(bind_rules).unwrap();
        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(true);
        checker.verify_sym_table_header(0);

        // Instruction section.
        checker.verify_instructions_header(UNCOND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES);
        checker.verify_debug_line_number(1);
        checker.verify_unconditional_abort();

        // Debug section.
        checker.verify_debug_header(107);
        checker.verify_debug_symbol_table_header(99);
        checker.verify_symbol_table(&[
            "fuchsia.BIND_PROTOCOL",
            "wagon",
            "fuchsia.BIND_USB_CLASS",
            "flower",
            "fuchsia.BIND_USB_VID",
        ]);
        checker.verify_end();
    }

    #[test]
    fn test_encode_debug_symbol_composite_bind_rules() {
        let composite_bind_rules = "composite grey_lourie;
            primary node \"go-away-bird\" {
                fuchsia.BIND_PROTOCOL == 1;
            }";

        let compiled_bind_rules =
            compile(&composite_bind_rules, &vec![], false, false, true, true).unwrap();

        assert_matches!(compiled_bind_rules, CompiledBindRules::CompositeBind(_));

        let bytecode;
        match compiled_bind_rules {
            CompiledBindRules::CompositeBind(rule) => {
                bytecode = encode_composite_to_bytecode(rule).unwrap();
            }
            _ => {
                panic!("Compiled bind rules are not composite");
            }
        }

        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(true);
        checker.verify_sym_table_header(33);
        checker.verify_symbol_table(&["grey_lourie", "go-away-bird"]);

        // Composite instruction section.
        let primary_node_bytes = COND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES;
        checker.verify_composite_header(
            NODE_HEADER_BYTES + COMPOSITE_NAME_ID_BYTES + primary_node_bytes,
        );

        // Each node section consists of the total node bytes from each
        // instruction and the debug line number bytes.
        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_debug_line_number(3);
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );

        // Debug section.
        checker.verify_debug_header(34);
        checker.verify_debug_symbol_table_header(26);
        checker.verify_symbol_table(&["fuchsia.BIND_PROTOCOL"]);
        checker.verify_end();
    }

    #[test]
    fn test_encode_debug_symbol_composite_bind_rules_with_optional() {
        let composite_bind_rules = "composite grey_lourie;
            primary node \"go-away-bird\" {
                fuchsia.BIND_PROTOCOL == 1;
            }
            optional node \"redpoll\" {
                fuchsia.BIND_FIDL_PROTOCOL == 2;
            }";

        let compiled_bind_rules =
            compile(&composite_bind_rules, &vec![], false, false, true, true).unwrap();

        assert_matches!(compiled_bind_rules, CompiledBindRules::CompositeBind(_));

        let bytecode;
        match compiled_bind_rules {
            CompiledBindRules::CompositeBind(rule) => {
                bytecode = encode_composite_to_bytecode(rule).unwrap();
            }
            _ => {
                panic!("Compiled bind rules are not composite");
            }
        }

        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(true);
        checker.verify_sym_table_header(45);
        checker.verify_symbol_table(&["grey_lourie", "go-away-bird", "redpoll"]);

        // Composite instruction section.
        let primary_node_bytes = COND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES;
        let optional_node_bytes = COND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES;
        checker.verify_composite_header(
            (NODE_HEADER_BYTES * 2)
                + COMPOSITE_NAME_ID_BYTES
                + primary_node_bytes
                + optional_node_bytes,
        );

        // Each node section consists of the total node bytes from each
        // instruction and the debug line number bytes.
        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_debug_line_number(3);
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );

        // Verify optional node.
        checker.verify_node_header(RawNodeType::Optional, 3, optional_node_bytes);
        checker.verify_debug_line_number(6);
        checker.verify_abort_not_equal(
            EncodedValue { value_type: RawValueType::NumberValue, value: 4 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2 },
        );

        // Debug section.
        checker.verify_debug_header(65);
        checker.verify_debug_symbol_table_header(57);
        checker.verify_symbol_table(&["fuchsia.BIND_PROTOCOL", "fuchsia.BIND_FIDL_PROTOCOL"]);
        checker.verify_end();
    }

    #[test]
    fn test_debug_line_number() {
        // AST Location for "fuchsia.BIND_PROTOCOL" and "rail".
        let location = AstLocation::AcceptStatementValue {
            identifier: make_identifier!["fuchsia.BIND_USB_CLASS"],
            value: Value::Identifier(make_identifier!["rail"]),
            span: Span { offset: 0, line: 9, fragment: " " },
        };

        let instruct_info = vec![SymbolicInstructionInfo {
            location: Some(location),
            instruction: SymbolicInstruction::UnconditionalAbort,
        }];

        let bind_rules = BindRules {
            instructions: instruct_info,
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: true,
        };

        let bytecode = encode_to_bytecode_v2(bind_rules).unwrap();
        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(true);
        checker.verify_sym_table_header(0);

        // Instruction section.
        checker.verify_instructions_header(UNCOND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES);
        checker.verify_debug_line_number(9);
        checker.verify_unconditional_abort();

        // Debug section.
        checker.verify_debug_header(44);
        checker.verify_debug_symbol_table_header(36);
        checker.verify_symbol_table(&["fuchsia.BIND_USB_CLASS", "rail"]);
        checker.verify_end();
    }

    #[test]
    fn test_debug_line_number_no_enable_debug() {
        // AST Location for "fuchsia.BIND_PROTOCOL" and "rail".
        let location = AstLocation::AcceptStatementValue {
            identifier: make_identifier!["fuchsia.BIND_USB_CLASS"],
            value: Value::Identifier(make_identifier!["rail"]),
            span: Span { offset: 0, line: 9, fragment: " " },
        };

        let instruct_info = vec![SymbolicInstructionInfo {
            location: Some(location),
            instruction: SymbolicInstruction::UnconditionalAbort,
        }];

        let bind_rules = BindRules {
            instructions: instruct_info,
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
            enable_debug: false,
        };

        let bytecode = encode_to_bytecode_v2(bind_rules).unwrap();
        let mut checker = BytecodeChecker::new(bytecode);

        checker.verify_bind_rules_header(false);
        checker.verify_sym_table_header(0);

        // Instruction section.
        checker.verify_instructions_header(UNCOND_ABORT_BYTES);
        checker.verify_unconditional_abort();

        // Debug section.
        checker.verify_end();
    }

    #[test]
    fn test_composite_debug_line_number() {
        let location = AstLocation::AcceptStatementValue {
            identifier: make_identifier!["fuchsia.BIND_USB_CLASS"],
            value: Value::Identifier(make_identifier!["rail"]),
            span: Span { offset: 0, line: 11, fragment: " " },
        };

        let primary_node_inst = vec![SymbolicInstructionInfo {
            location: Some(location),
            instruction: SymbolicInstruction::UnconditionalAbort,
        }];

        let bind_rules = CompositeBindRules {
            device_name: "treehunter".to_string(),
            symbol_table: HashMap::new(),
            additional_nodes: vec![],
            optional_nodes: vec![],
            primary_node: CompositeNode {
                name: "bananaquit".to_string(),
                instructions: primary_node_inst,
            },
            enable_debug: true,
        };

        let mut checker = BytecodeChecker::new(encode_composite_to_bytecode(bind_rules).unwrap());
        checker.verify_bind_rules_header(true);

        // Symbol section.
        checker.verify_sym_table_header(30);
        checker.verify_symbol_table(&["treehunter", "bananaquit"]);

        // Composite instruction section.
        let primary_node_bytes = UNCOND_ABORT_BYTES + DEBG_LINE_NUMBER_BYTES;
        checker.verify_composite_header(
            NODE_HEADER_BYTES + COMPOSITE_NAME_ID_BYTES + primary_node_bytes,
        );

        checker.verify_node_header(RawNodeType::Primary, 2, primary_node_bytes);
        checker.verify_debug_line_number(11);
        checker.verify_unconditional_abort();

        // Debug section.
        checker.verify_debug_header(44);
        checker.verify_debug_symbol_table_header(36);
        checker.verify_symbol_table(&["fuchsia.BIND_USB_CLASS", "rail"]);

        checker.verify_end();
    }
}
