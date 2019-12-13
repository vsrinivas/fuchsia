// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler::{self, Symbol, SymbolTable, SymbolicInstruction};
use crate::device_specification::{self, Property};
use crate::errors::{self, UserError};
use crate::parser_common::{self, CompoundIdentifier, Value};
use std::collections::{HashMap, HashSet};
use std::fmt;
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;
use std::str::FromStr;

#[derive(Debug, Clone, PartialEq)]
pub enum DebuggerError {
    FileError(errors::FileError),
    BindParserError(parser_common::BindParserError),
    CompilerError(compiler::CompilerError),
    DuplicateKeyError(CompoundIdentifier),
    MissingLabelError,
    NoOutcomeError,
}

impl fmt::Display for DebuggerError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

pub fn debug(
    program: PathBuf,
    libraries: &[PathBuf],
    device_file: PathBuf,
) -> Result<(), DebuggerError> {
    let mut file = File::open(&device_file).map_err(|_| {
        DebuggerError::FileError(errors::FileError::FileOpenError(device_file.clone()))
    })?;
    let mut buf = String::new();
    file.read_to_string(&mut buf).map_err(|_| {
        DebuggerError::FileError(errors::FileError::FileReadError(device_file.clone()))
    })?;
    let ast = device_specification::Ast::from_str(&buf).map_err(DebuggerError::BindParserError)?;

    let (instructions, symbol_table) =
        compiler::compile_to_symbolic(program, libraries).map_err(DebuggerError::CompilerError)?;

    let symbolic_properties = properties_to_symbols(&ast.properties, &symbol_table)?;

    let binds = evaluate_bind_program(&instructions, &symbolic_properties)?;

    if binds {
        println!("Binds to device.");
    } else {
        println!("Doesn't bind to device.");
    }

    Ok(())
}

fn properties_to_symbols(
    properties: &[Property],
    symbol_table: &SymbolTable,
) -> Result<HashMap<Symbol, Symbol>, DebuggerError> {
    let mut symbolic_properties = HashMap::new();
    let mut keys_seen = HashSet::new();

    for property in properties {
        // Check for duplicate keys in the device specification.
        // This is done using a separte set since the symbolic_properties map only contains
        // keys and values which are present in the bind program's symbol table.
        if keys_seen.contains(&property.key) {
            return Err(DebuggerError::DuplicateKeyError(property.key.clone()));
        }
        keys_seen.insert(property.key.clone());

        let key_symbol = symbol_table.get(&property.key);

        let value_symbol = match &property.value {
            Value::NumericLiteral(n) => Some(Symbol::NumberValue(*n)),
            Value::StringLiteral(s) => Some(Symbol::StringValue(s.to_string())),
            Value::BoolLiteral(b) => Some(Symbol::BoolValue(*b)),
            Value::Identifier(identifier) => match symbol_table.get(&identifier) {
                Some(symbol) => Some(symbol.clone()),
                None => None,
            },
        };

        if let (Some(key), Some(value)) = (key_symbol, value_symbol) {
            symbolic_properties.insert(key.clone(), value);
        }
    }

    Ok(symbolic_properties)
}

fn evaluate_bind_program(
    instructions: &Vec<SymbolicInstruction>,
    properties: &HashMap<Symbol, Symbol>,
) -> Result<bool, DebuggerError> {
    let mut instructions = instructions.iter();

    while let Some(mut instruction) = instructions.next() {
        let mut jump_label = None;

        match instruction {
            SymbolicInstruction::AbortIfEqual { lhs, rhs } => {
                if properties.get(lhs) == Some(rhs) {
                    return Ok(false);
                }
            }
            SymbolicInstruction::AbortIfNotEqual { lhs, rhs } => {
                if properties.get(lhs) != Some(rhs) {
                    return Ok(false);
                }
            }
            SymbolicInstruction::Label(_label) => (),
            SymbolicInstruction::UnconditionalJump { label } => {
                jump_label = Some(label);
            }
            SymbolicInstruction::JumpIfEqual { lhs, rhs, label } => {
                if properties.get(lhs) == Some(rhs) {
                    jump_label = Some(label);
                }
            }
            SymbolicInstruction::JumpIfNotEqual { lhs, rhs, label } => {
                if properties.get(lhs) != Some(rhs) {
                    jump_label = Some(label);
                }
            }
            SymbolicInstruction::UnconditionalAbort => return Ok(false),
            SymbolicInstruction::UnconditionalBind => return Ok(true),
        }

        if let Some(label) = jump_label {
            while instruction != &SymbolicInstruction::Label(*label) {
                instruction = instructions.next().ok_or(DebuggerError::MissingLabelError)?;
            }
        }
    }

    Err(DebuggerError::NoOutcomeError)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bind_library;
    use crate::bind_program::{Condition, ConditionOp, Statement};
    use crate::make_identifier;
    use crate::parser_common::CompoundIdentifier;

    #[test]
    fn duplicate_key() {
        let symbol_table = HashMap::new();
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) },
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(5) },
        ];
        assert_eq!(
            properties_to_symbols(&properties, &symbol_table),
            Err(DebuggerError::DuplicateKeyError(make_identifier!("abc")))
        );
    }

    #[test]
    fn condition_equals() {
        let statements = vec![Statement::ConditionStatement(Condition {
            lhs: make_identifier!("abc"),
            op: ConditionOp::Equals,
            rhs: Value::NumericLiteral(42),
        })];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );
        let instructions = compiler::compile_statements(statements, &symbol_table).unwrap();

        // Binds when the device has the correct property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Binds when other properties are present as well.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) },
            Property { key: make_identifier!("xyz"), value: Value::BoolLiteral(true) },
        ];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind when the device has the wrong value for the property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(5) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind when the property is not present in the device.
        let properties = Vec::new();
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());
    }

    #[test]
    fn condition_not_equals() {
        let statements = vec![Statement::ConditionStatement(Condition {
            lhs: make_identifier!("abc"),
            op: ConditionOp::NotEquals,
            rhs: Value::NumericLiteral(42),
        })];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );
        let instructions = compiler::compile_statements(statements, &symbol_table).unwrap();

        // Binds when the device has a different value for the property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(5) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Binds when the property is not present in the device.
        let properties = Vec::new();
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind when the device has the property in the condition statement.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());
    }

    #[test]
    fn accept() {
        let statements = vec![Statement::Accept {
            identifier: make_identifier!("abc"),
            values: vec![Value::NumericLiteral(42), Value::NumericLiteral(314)],
        }];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        let instructions = compiler::compile_statements(statements, &symbol_table).unwrap();

        // Binds when the device has one of the accepted values for the property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind when the device has a different value for the property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(5) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind when the device is missing the property.
        let properties = Vec::new();
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());
    }

    #[test]
    fn if_else() {
        let statements = vec![Statement::If {
            blocks: vec![
                (
                    Condition {
                        lhs: make_identifier!("abc"),
                        op: ConditionOp::Equals,
                        rhs: Value::NumericLiteral(1),
                    },
                    vec![Statement::ConditionStatement(Condition {
                        lhs: make_identifier!("xyz"),
                        op: ConditionOp::Equals,
                        rhs: Value::NumericLiteral(1),
                    })],
                ),
                (
                    Condition {
                        lhs: make_identifier!("abc"),
                        op: ConditionOp::Equals,
                        rhs: Value::NumericLiteral(2),
                    },
                    vec![Statement::ConditionStatement(Condition {
                        lhs: make_identifier!("xyz"),
                        op: ConditionOp::Equals,
                        rhs: Value::NumericLiteral(2),
                    })],
                ),
            ],
            else_block: vec![Statement::ConditionStatement(Condition {
                lhs: make_identifier!("xyz"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(3),
            })],
        }];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );
        symbol_table.insert(
            make_identifier!("xyz"),
            Symbol::Key("xyz".to_string(), bind_library::ValueType::Number),
        );

        let instructions = compiler::compile_statements(statements, &symbol_table).unwrap();

        // Binds when the if clause is satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(1) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(1) },
        ];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Binds when the if else clause is satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(2) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(2) },
        ];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Binds when the else clause is satisfied.
        let properties =
            vec![Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(3) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind when the device has incorrect values for the properties.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(42) },
        ];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind when the properties are missing in the device.
        let properties = Vec::new();
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());
    }

    #[test]
    fn abort() {
        let statements = vec![
            Statement::ConditionStatement(Condition {
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(42),
            }),
            Statement::Abort,
        ];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        let instructions = compiler::compile_statements(statements, &symbol_table).unwrap();

        // Doesn't bind when abort statement is present.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());
    }

    #[test]
    fn supports_all_value_types() {
        let statements = vec![
            Statement::ConditionStatement(Condition {
                lhs: make_identifier!("a"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(42),
            }),
            Statement::ConditionStatement(Condition {
                lhs: make_identifier!("b"),
                op: ConditionOp::Equals,
                rhs: Value::BoolLiteral(false),
            }),
            Statement::ConditionStatement(Condition {
                lhs: make_identifier!("c"),
                op: ConditionOp::Equals,
                rhs: Value::StringLiteral("string".to_string()),
            }),
            Statement::ConditionStatement(Condition {
                lhs: make_identifier!("d"),
                op: ConditionOp::Equals,
                rhs: Value::Identifier(make_identifier!("VALUE")),
            }),
        ];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("a"),
            Symbol::Key("a".to_string(), bind_library::ValueType::Number),
        );
        symbol_table.insert(
            make_identifier!("b"),
            Symbol::Key("b".to_string(), bind_library::ValueType::Number),
        );
        symbol_table.insert(
            make_identifier!("c"),
            Symbol::Key("c".to_string(), bind_library::ValueType::Number),
        );
        symbol_table.insert(
            make_identifier!("d"),
            Symbol::Key("d".to_string(), bind_library::ValueType::Number),
        );
        symbol_table.insert(
            make_identifier!("VALUE"),
            Symbol::Key("VALUE".to_string(), bind_library::ValueType::Number),
        );

        let instructions = compiler::compile_statements(statements, &symbol_table).unwrap();

        // Binds when other properties are present as well.
        let properties = vec![
            Property { key: make_identifier!("a"), value: Value::NumericLiteral(42) },
            Property { key: make_identifier!("b"), value: Value::BoolLiteral(false) },
            Property {
                key: make_identifier!("c"),
                value: Value::StringLiteral("string".to_string()),
            },
            Property {
                key: make_identifier!("d"),
                value: Value::Identifier(make_identifier!("VALUE")),
            },
        ];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());
    }

    #[test]
    fn full_program() {
        let statements = vec![Statement::If {
            blocks: vec![(
                Condition {
                    lhs: make_identifier!("abc"),
                    op: ConditionOp::Equals,
                    rhs: Value::NumericLiteral(42),
                },
                vec![Statement::Abort],
            )],
            else_block: vec![
                Statement::Accept {
                    identifier: make_identifier!("xyz"),
                    values: vec![Value::NumericLiteral(1), Value::NumericLiteral(2)],
                },
                Statement::ConditionStatement(Condition {
                    lhs: make_identifier!("pqr"),
                    op: ConditionOp::NotEquals,
                    rhs: Value::BoolLiteral(true),
                }),
            ],
        }];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );
        symbol_table.insert(
            make_identifier!("xyz"),
            Symbol::Key("xyz".to_string(), bind_library::ValueType::Number),
        );
        symbol_table.insert(
            make_identifier!("pqr"),
            Symbol::Key("pqr".to_string(), bind_library::ValueType::Number),
        );
        let instructions = compiler::compile_statements(statements, &symbol_table).unwrap();

        // Aborts because if condition is true.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Binds because all statements inside else block are satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(43) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(1) },
        ];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind because accept statement is not satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(43) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(3) },
        ];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());

        // Doesn't bind because condition statement is not satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(43) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(1) },
            Property { key: make_identifier!("pqr"), value: Value::BoolLiteral(true) },
        ];
        let symbolic_properties = properties_to_symbols(&properties, &symbol_table).unwrap();
        assert!(!evaluate_bind_program(&instructions, &symbolic_properties).unwrap());
    }
}
