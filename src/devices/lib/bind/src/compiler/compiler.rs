// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bytecode_encoder::encode_v1::encode_to_bytecode_v1;
use crate::bytecode_encoder::encode_v2::{encode_composite_to_bytecode, encode_to_bytecode_v2};
use crate::bytecode_encoder::error::BindRulesEncodeError;
use crate::compiler::symbol_table::*;
use crate::compiler::{dependency_graph, instruction};
use crate::ddk_bind_constants::BIND_AUTOBIND;
use crate::debugger::offline_debugger::AstLocation;
use crate::errors::UserError;
use crate::linter;
use crate::parser::bind_rules::{self, Condition, ConditionOp, Statement};
use crate::parser::common::{CompoundIdentifier, Value};
use crate::parser::{self, bind_composite};
use std::collections::HashMap;
use std::convert::TryFrom;
use std::fmt;
use thiserror::Error;

#[derive(Debug, Error, Clone, PartialEq)]
pub enum CompilerError {
    BindParserError(parser::common::BindParserError),
    DependencyError(dependency_graph::DependencyError<CompoundIdentifier>),
    LinterError(linter::LinterError),
    DuplicateIdentifier(CompoundIdentifier),
    TypeMismatch(CompoundIdentifier),
    UnresolvedQualification(CompoundIdentifier),
    UndeclaredKey(CompoundIdentifier),
    MissingExtendsKeyword(CompoundIdentifier),
    InvalidExtendsKeyword(CompoundIdentifier),
    UnknownKey(CompoundIdentifier),
    IfStatementMustBeTerminal,
    TrueStatementMustBeIsolated,
    FalseStatementMustBeIsolated,
}

impl fmt::Display for CompilerError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

#[derive(Debug, Error, Clone, PartialEq)]
pub enum BindRulesDecodeError {
    InvalidBinaryLength,
}

impl fmt::Display for BindRulesDecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

#[derive(Debug, PartialEq)]
pub enum CompiledBindRules<'a> {
    Bind(BindRules<'a>),
    CompositeBind(CompositeBindRules<'a>),
}

impl<'a> CompiledBindRules<'a> {
    pub fn encode_to_bytecode(self) -> Result<Vec<u8>, BindRulesEncodeError> {
        match self {
            CompiledBindRules::Bind(bind_rules) => {
                if bind_rules.use_new_bytecode {
                    return encode_to_bytecode_v2(bind_rules);
                }

                encode_to_bytecode_v1(bind_rules)
            }
            CompiledBindRules::CompositeBind(composite_bind) => {
                encode_composite_to_bytecode(composite_bind)
            }
        }
    }

    pub fn empty_bind_rules(
        use_new_bytecode: bool,
        disable_autobind: bool,
    ) -> CompiledBindRules<'a> {
        let mut instructions = vec![];

        if disable_autobind {
            instructions.push(SymbolicInstructionInfo::disable_autobind());
        }

        if !use_new_bytecode {
            instructions.push(SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalBind,
            });
        }

        CompiledBindRules::Bind(BindRules {
            instructions: instructions,
            symbol_table: HashMap::new(),
            use_new_bytecode: use_new_bytecode,
        })
    }
}

#[derive(Debug, PartialEq)]
pub struct BindRules<'a> {
    pub symbol_table: SymbolTable,
    pub instructions: Vec<SymbolicInstructionInfo<'a>>,
    pub use_new_bytecode: bool,
}

#[derive(Debug, PartialEq)]
pub enum SymbolicInstruction {
    AbortIfEqual { lhs: Symbol, rhs: Symbol },
    AbortIfNotEqual { lhs: Symbol, rhs: Symbol },
    Label(u32),
    UnconditionalJump { label: u32 },
    JumpIfEqual { lhs: Symbol, rhs: Symbol, label: u32 },
    JumpIfNotEqual { lhs: Symbol, rhs: Symbol, label: u32 },
    UnconditionalAbort,
    UnconditionalBind,
}

impl SymbolicInstruction {
    pub fn to_instruction(self) -> instruction::Instruction {
        match self {
            SymbolicInstruction::AbortIfEqual { lhs, rhs } => {
                instruction::Instruction::Abort(instruction::Condition::Equal(lhs, rhs))
            }
            SymbolicInstruction::AbortIfNotEqual { lhs, rhs } => {
                instruction::Instruction::Abort(instruction::Condition::NotEqual(lhs, rhs))
            }
            SymbolicInstruction::Label(label_id) => instruction::Instruction::Label(label_id),
            SymbolicInstruction::UnconditionalJump { label } => {
                instruction::Instruction::Goto(instruction::Condition::Always, label)
            }
            SymbolicInstruction::JumpIfEqual { lhs, rhs, label } => {
                instruction::Instruction::Goto(instruction::Condition::Equal(lhs, rhs), label)
            }
            SymbolicInstruction::JumpIfNotEqual { lhs, rhs, label } => {
                instruction::Instruction::Goto(instruction::Condition::NotEqual(lhs, rhs), label)
            }
            SymbolicInstruction::UnconditionalAbort => {
                instruction::Instruction::Abort(instruction::Condition::Always)
            }
            SymbolicInstruction::UnconditionalBind => {
                instruction::Instruction::Match(instruction::Condition::Always)
            }
        }
    }
}

#[derive(Debug, PartialEq)]
pub struct SymbolicInstructionInfo<'a> {
    pub location: Option<AstLocation<'a>>,
    pub instruction: SymbolicInstruction,
}

impl<'a> SymbolicInstructionInfo<'a> {
    pub fn to_instruction(self) -> instruction::InstructionInfo {
        instruction::InstructionInfo {
            instruction: self.instruction.to_instruction(),
            debug: match self.location {
                Some(location) => location.to_instruction_debug(),
                None => instruction::InstructionDebug::none(),
            },
        }
    }

    pub fn disable_autobind() -> Self {
        SymbolicInstructionInfo {
            location: None,
            instruction: SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::DeprecatedKey(BIND_AUTOBIND),
                rhs: Symbol::NumberValue(0),
            },
        }
    }
}

#[derive(Debug, PartialEq)]
pub struct CompositeNode<'a> {
    pub name: String,
    pub instructions: Vec<SymbolicInstructionInfo<'a>>,
}

#[derive(Debug, PartialEq)]
pub struct CompositeBindRules<'a> {
    pub device_name: String,
    pub symbol_table: SymbolTable,
    pub primary_node: CompositeNode<'a>,
    pub additional_nodes: Vec<CompositeNode<'a>>,
}

pub fn compile<'a>(
    rules_str: &'a str,
    libraries: &[String],
    lint: bool,
    disable_autobind: bool,
    use_new_bytecode: bool,
) -> Result<CompiledBindRules<'a>, CompilerError> {
    if bind_composite::Ast::try_from(rules_str).is_ok() {
        return Ok(CompiledBindRules::CompositeBind(compile_bind_composite(
            rules_str,
            libraries,
            lint,
            use_new_bytecode,
        )?));
    }

    Ok(CompiledBindRules::Bind(compile_bind(
        rules_str,
        libraries,
        lint,
        disable_autobind,
        use_new_bytecode,
    )?))
}

pub fn compile_bind<'a>(
    rules_str: &'a str,
    libraries: &[String],
    lint: bool,
    disable_autobind: bool,
    use_new_bytecode: bool,
) -> Result<BindRules<'a>, CompilerError> {
    let ast = bind_rules::Ast::try_from(rules_str).map_err(CompilerError::BindParserError)?;
    let symbol_table = get_symbol_table_from_libraries(&ast.using, libraries, lint)?;

    let mut instructions = compile_statements(ast.statements, &symbol_table, use_new_bytecode)?;
    if disable_autobind {
        instructions.insert(0, SymbolicInstructionInfo::disable_autobind());
    }

    Ok(BindRules {
        symbol_table: symbol_table,
        instructions: instructions,
        use_new_bytecode: use_new_bytecode,
    })
}

pub fn compile_bind_composite<'a>(
    rules_str: &'a str,
    libraries: &[String],
    lint: bool,
    use_new_bytecode: bool,
) -> Result<CompositeBindRules<'a>, CompilerError> {
    let ast = bind_composite::Ast::try_from(rules_str).map_err(CompilerError::BindParserError)?;
    let symbol_table = get_symbol_table_from_libraries(&ast.using, libraries, lint)?;
    let primary_node = CompositeNode {
        name: ast.primary_node.name,
        instructions: compile_statements(
            ast.primary_node.statements,
            &symbol_table,
            use_new_bytecode,
        )?,
    };
    let additional_nodes = ast
        .nodes
        .into_iter()
        .map(|node| {
            let name = node.name;
            compile_statements(node.statements, &symbol_table, use_new_bytecode)
                .map(|inst| CompositeNode { name: name, instructions: inst })
        })
        .collect::<Result<Vec<CompositeNode<'_>>, CompilerError>>()?;

    Ok(CompositeBindRules {
        device_name: ast.name.to_string(),
        symbol_table: symbol_table,
        primary_node: primary_node,
        additional_nodes: additional_nodes,
    })
}

pub fn compile_statements<'a, 'b>(
    statements: Vec<Statement<'a>>,
    symbol_table: &'b SymbolTable,
    use_new_bytecode: bool,
) -> Result<Vec<SymbolicInstructionInfo<'a>>, CompilerError> {
    let mut compiler = Compiler::new(symbol_table);
    compiler.compile_statements(statements, use_new_bytecode)?;
    Ok(compiler.instructions)
}

struct Compiler<'a, 'b> {
    symbol_table: &'b SymbolTable,
    pub instructions: Vec<SymbolicInstructionInfo<'a>>,
    next_label_id: u32,
}

impl<'a, 'b> Compiler<'a, 'b> {
    fn new(symbol_table: &'b SymbolTable) -> Self {
        Compiler { symbol_table: symbol_table, instructions: vec![], next_label_id: 0 }
    }

    fn lookup_identifier(&self, identifier: &CompoundIdentifier) -> Result<Symbol, CompilerError> {
        let symbol = self
            .symbol_table
            .get(identifier)
            .ok_or(CompilerError::UnknownKey(identifier.clone()))?;
        Ok(symbol.clone())
    }

    fn lookup_value(&self, value: &Value) -> Result<Symbol, CompilerError> {
        match value {
            Value::NumericLiteral(n) => Ok(Symbol::NumberValue(*n)),
            Value::StringLiteral(s) => Ok(Symbol::StringValue(s.to_string())),
            Value::BoolLiteral(b) => Ok(Symbol::BoolValue(*b)),
            Value::Identifier(ident) => self
                .symbol_table
                .get(ident)
                .ok_or(CompilerError::UnknownKey(ident.clone()))
                .map(|x| x.clone()),
        }
    }

    fn compile_statements(
        &mut self,
        statements: Vec<Statement<'a>>,
        use_new_bytecode: bool,
    ) -> Result<(), CompilerError> {
        self.compile_block(statements)?;

        // If none of the statements caused an abort, then we should bind the driver.
        if !use_new_bytecode {
            self.instructions.push(SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalBind,
            });
        }

        Ok(())
    }

    fn get_unique_label(&mut self) -> u32 {
        let label = self.next_label_id;
        self.next_label_id += 1;
        label
    }

    fn compile_block(&mut self, statements: Vec<Statement<'a>>) -> Result<(), CompilerError> {
        let num_statements = statements.len();
        let mut iter = statements.into_iter().peekable();
        while let Some(statement) = iter.next() {
            match statement {
                Statement::ConditionStatement { .. } => {
                    if let Statement::ConditionStatement {
                        span: _,
                        condition: Condition { span: _, lhs, op, rhs },
                    } = &statement
                    {
                        let lhs_symbol = self.lookup_identifier(lhs)?;
                        let rhs_symbol = self.lookup_value(rhs)?;
                        let instruction = match op {
                            ConditionOp::Equals => SymbolicInstruction::AbortIfNotEqual {
                                lhs: lhs_symbol,
                                rhs: rhs_symbol,
                            },
                            ConditionOp::NotEquals => SymbolicInstruction::AbortIfEqual {
                                lhs: lhs_symbol,
                                rhs: rhs_symbol,
                            },
                        };
                        self.instructions.push(SymbolicInstructionInfo {
                            location: Some(AstLocation::ConditionStatement(statement)),
                            instruction,
                        });
                    }
                }
                Statement::Accept { span, identifier, values } => {
                    let lhs_symbol = self.lookup_identifier(&identifier)?;
                    let label_id = self.get_unique_label();
                    for value in values {
                        self.instructions.push(SymbolicInstructionInfo {
                            location: Some(AstLocation::AcceptStatementValue {
                                identifier: identifier.clone(),
                                value: value.clone(),
                                span: span.clone(),
                            }),
                            instruction: SymbolicInstruction::JumpIfEqual {
                                lhs: lhs_symbol.clone(),
                                rhs: self.lookup_value(&value)?,
                                label: label_id,
                            },
                        });
                    }
                    self.instructions.push(SymbolicInstructionInfo {
                        location: Some(AstLocation::AcceptStatementFailure {
                            identifier,
                            symbol: lhs_symbol,
                            span,
                        }),
                        instruction: SymbolicInstruction::UnconditionalAbort,
                    });
                    self.instructions.push(SymbolicInstructionInfo {
                        location: None,
                        instruction: SymbolicInstruction::Label(label_id),
                    });
                }
                Statement::If { span: _, blocks, else_block } => {
                    if !iter.peek().is_none() {
                        return Err(CompilerError::IfStatementMustBeTerminal);
                    }

                    let final_label_id = self.get_unique_label();

                    for (condition, block_statements) in blocks {
                        let Condition { span: _, lhs, op, rhs } = &condition;

                        let lhs_symbol = self.lookup_identifier(lhs)?;
                        let rhs_symbol = self.lookup_value(rhs)?;

                        // Generate instructions for the condition.
                        let label_id = self.get_unique_label();
                        let instruction = match op {
                            ConditionOp::Equals => SymbolicInstruction::JumpIfNotEqual {
                                lhs: lhs_symbol,
                                rhs: rhs_symbol,
                                label: label_id,
                            },
                            ConditionOp::NotEquals => SymbolicInstruction::JumpIfEqual {
                                lhs: lhs_symbol,
                                rhs: rhs_symbol,
                                label: label_id,
                            },
                        };
                        self.instructions.push(SymbolicInstructionInfo {
                            location: Some(AstLocation::IfCondition(condition)),
                            instruction,
                        });

                        // Compile the block itself.
                        self.compile_block(block_statements)?;

                        // Jump to after the if statement.
                        self.instructions.push(SymbolicInstructionInfo {
                            location: None,
                            instruction: SymbolicInstruction::UnconditionalJump {
                                label: final_label_id,
                            },
                        });

                        // Insert a label to jump to when the condition fails.
                        self.instructions.push(SymbolicInstructionInfo {
                            location: None,
                            instruction: SymbolicInstruction::Label(label_id),
                        });
                    }

                    // Compile the else block.
                    self.compile_block(else_block)?;

                    // Insert a label to jump to at the end of the whole if statement. Note that we
                    // could just emit an unconditional bind instead of jumping, since we know that
                    // if statements are terminal, but we do the jump to be consistent with
                    // condition and accept statements.

                    self.instructions.push(SymbolicInstructionInfo {
                        location: None,
                        instruction: SymbolicInstruction::Label(final_label_id),
                    });
                }
                Statement::False { span: _ } => {
                    if num_statements != 1 {
                        return Err(CompilerError::FalseStatementMustBeIsolated);
                    }
                    self.instructions.push(SymbolicInstructionInfo {
                        location: Some(AstLocation::FalseStatement(statement)),
                        instruction: SymbolicInstruction::UnconditionalAbort,
                    });
                }
                Statement::True { .. } => {
                    // A `true` statement doesn't require an instruction.
                    if num_statements != 1 {
                        return Err(CompilerError::TrueStatementMustBeIsolated);
                    }
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;
    use crate::parser::bind_library;
    use crate::parser::common::{Include, Span};

    #[test]
    fn condition() {
        let condition_statement = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(42),
            },
        };

        let rules =
            bind_rules::Ast { using: vec![], statements: vec![condition_statement.clone()] };
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        assert_eq!(
            compile_statements(rules.statements, &symbol_table, false).unwrap(),
            vec![
                SymbolicInstructionInfo {
                    location: Some(AstLocation::ConditionStatement(condition_statement)),
                    instruction: SymbolicInstruction::AbortIfNotEqual {
                        lhs: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        rhs: Symbol::NumberValue(42)
                    }
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::UnconditionalBind
                }
            ]
        );
    }

    #[test]
    fn accept() {
        let rules = bind_rules::Ast {
            using: vec![],
            statements: vec![Statement::Accept {
                span: Span::new(),
                identifier: make_identifier!("abc"),
                values: vec![Value::NumericLiteral(42), Value::NumericLiteral(314)],
            }],
        };
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        assert_eq!(
            compile_statements(rules.statements, &symbol_table, false).unwrap(),
            vec![
                SymbolicInstructionInfo {
                    location: Some(AstLocation::AcceptStatementValue {
                        identifier: make_identifier!("abc"),
                        value: Value::NumericLiteral(42),
                        span: Span::new()
                    }),
                    instruction: SymbolicInstruction::JumpIfEqual {
                        lhs: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        rhs: Symbol::NumberValue(42),
                        label: 0
                    }
                },
                SymbolicInstructionInfo {
                    location: Some(AstLocation::AcceptStatementValue {
                        identifier: make_identifier!("abc"),
                        value: Value::NumericLiteral(314),
                        span: Span::new()
                    }),
                    instruction: SymbolicInstruction::JumpIfEqual {
                        lhs: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        rhs: Symbol::NumberValue(314),
                        label: 0
                    }
                },
                SymbolicInstructionInfo {
                    location: Some(AstLocation::AcceptStatementFailure {
                        identifier: make_identifier!("abc"),
                        symbol: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        span: Span::new()
                    }),
                    instruction: SymbolicInstruction::UnconditionalAbort
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::Label(0)
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::UnconditionalBind
                },
            ]
        );
    }

    #[test]
    fn if_else() {
        let condition1 = Condition {
            span: Span::new(),
            lhs: make_identifier!("abc"),
            op: ConditionOp::Equals,
            rhs: Value::NumericLiteral(1),
        };
        let condition2 = Condition {
            span: Span::new(),
            lhs: make_identifier!("abc"),
            op: ConditionOp::Equals,
            rhs: Value::NumericLiteral(2),
        };
        let statement1 = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(2),
            },
        };
        let statement2 = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(3),
            },
        };
        let statement3 = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(3),
            },
        };

        let rules = bind_rules::Ast {
            using: vec![],
            statements: vec![Statement::If {
                span: Span::new(),
                blocks: vec![
                    (condition1.clone(), vec![statement1.clone()]),
                    (condition2.clone(), vec![statement2.clone()]),
                ],
                else_block: vec![statement3.clone()],
            }],
        };
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        assert_eq!(
            compile_statements(rules.statements, &symbol_table, false).unwrap(),
            vec![
                SymbolicInstructionInfo {
                    location: Some(AstLocation::IfCondition(condition1)),
                    instruction: SymbolicInstruction::JumpIfNotEqual {
                        lhs: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        rhs: Symbol::NumberValue(1),
                        label: 1
                    }
                },
                SymbolicInstructionInfo {
                    location: Some(AstLocation::ConditionStatement(statement1)),
                    instruction: SymbolicInstruction::AbortIfNotEqual {
                        lhs: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        rhs: Symbol::NumberValue(2)
                    }
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::UnconditionalJump { label: 0 }
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::Label(1)
                },
                SymbolicInstructionInfo {
                    location: Some(AstLocation::IfCondition(condition2)),
                    instruction: SymbolicInstruction::JumpIfNotEqual {
                        lhs: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        rhs: Symbol::NumberValue(2),
                        label: 2
                    }
                },
                SymbolicInstructionInfo {
                    location: Some(AstLocation::ConditionStatement(statement2)),
                    instruction: SymbolicInstruction::AbortIfNotEqual {
                        lhs: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        rhs: Symbol::NumberValue(3)
                    }
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::UnconditionalJump { label: 0 }
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::Label(2)
                },
                SymbolicInstructionInfo {
                    location: Some(AstLocation::ConditionStatement(statement3)),
                    instruction: SymbolicInstruction::AbortIfNotEqual {
                        lhs: Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
                        rhs: Symbol::NumberValue(3)
                    }
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::Label(0)
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::UnconditionalBind
                },
            ]
        );
    }

    #[test]
    fn if_else_must_be_terminal() {
        let rules = bind_rules::Ast {
            using: vec![],
            statements: vec![
                Statement::If {
                    span: Span::new(),
                    blocks: vec![(
                        Condition {
                            span: Span::new(),
                            lhs: make_identifier!("abc"),
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(1),
                        },
                        vec![Statement::ConditionStatement {
                            span: Span::new(),
                            condition: Condition {
                                span: Span::new(),
                                lhs: make_identifier!("abc"),
                                op: ConditionOp::Equals,
                                rhs: Value::NumericLiteral(2),
                            },
                        }],
                    )],
                    else_block: vec![Statement::ConditionStatement {
                        span: Span::new(),
                        condition: Condition {
                            span: Span::new(),
                            lhs: make_identifier!("abc"),
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(3),
                        },
                    }],
                },
                Statement::Accept {
                    span: Span::new(),
                    identifier: make_identifier!("abc"),
                    values: vec![Value::NumericLiteral(42), Value::NumericLiteral(314)],
                },
            ],
        };
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        assert_eq!(
            compile_statements(rules.statements, &symbol_table, false),
            Err(CompilerError::IfStatementMustBeTerminal)
        );
    }

    #[test]
    fn false_statement() {
        let abort_statement = Statement::False { span: Span::new() };

        let rules = bind_rules::Ast { using: vec![], statements: vec![abort_statement.clone()] };
        let symbol_table = HashMap::new();

        assert_eq!(
            compile_statements(rules.statements, &symbol_table, false).unwrap(),
            vec![
                SymbolicInstructionInfo {
                    location: Some(AstLocation::FalseStatement(abort_statement)),
                    instruction: SymbolicInstruction::UnconditionalAbort
                },
                SymbolicInstructionInfo {
                    location: None,
                    instruction: SymbolicInstruction::UnconditionalBind
                }
            ]
        );
    }

    #[test]
    fn false_statement_must_be_isolated() {
        let condition_statement = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(42),
            },
        };
        let abort_statement = Statement::False { span: Span::new() };

        let rules = bind_rules::Ast {
            using: vec![],
            statements: vec![condition_statement.clone(), abort_statement.clone()],
        };
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        assert_eq!(
            compile_statements(rules.statements, &symbol_table, false),
            Err(CompilerError::FalseStatementMustBeIsolated)
        );
    }

    #[test]
    fn true_statement_must_be_isolated() {
        let condition_statement = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(42),
            },
        };
        let abort_statement = Statement::True { span: Span::new() };

        let rules = bind_rules::Ast {
            using: vec![],
            statements: vec![condition_statement.clone(), abort_statement.clone()],
        };
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        assert_eq!(
            compile_statements(rules.statements, &symbol_table, false),
            Err(CompilerError::TrueStatementMustBeIsolated)
        );
    }

    #[test]
    fn dependencies() {
        let rules = bind_rules::Ast {
            using: vec![Include { name: make_identifier!("A"), alias: None }],
            statements: vec![],
        };
        let libraries = vec![
            bind_library::Ast {
                name: make_identifier!("A"),
                using: vec![Include { name: make_identifier!("A", "B"), alias: None }],
                declarations: vec![],
            },
            bind_library::Ast {
                name: make_identifier!("A", "B"),
                using: vec![],
                declarations: vec![],
            },
            bind_library::Ast {
                name: make_identifier!("A", "C"),
                using: vec![],
                declarations: vec![],
            },
        ];

        assert_eq!(
            resolve_dependencies(&rules.using, libraries.iter()),
            Ok(vec![
                &bind_library::Ast {
                    name: make_identifier!("A"),
                    using: vec![Include { name: make_identifier!("A", "B"), alias: None }],
                    declarations: vec![],
                },
                &bind_library::Ast {
                    name: make_identifier!("A", "B"),
                    using: vec![],
                    declarations: vec![],
                },
            ])
        );
    }

    #[test]
    fn dependencies_error() {
        let rules = bind_rules::Ast {
            using: vec![Include { name: make_identifier!("A"), alias: None }],
            statements: vec![],
        };
        let libraries = vec![
            bind_library::Ast {
                name: make_identifier!("A"),
                using: vec![Include { name: make_identifier!("A", "B"), alias: None }],
                declarations: vec![],
            },
            bind_library::Ast {
                name: make_identifier!("A", "C"),
                using: vec![],
                declarations: vec![],
            },
        ];

        assert_eq!(
            resolve_dependencies(&rules.using, libraries.iter()),
            Err(CompilerError::DependencyError(
                dependency_graph::DependencyError::MissingDependency(make_identifier!("A", "B"))
            ))
        );
    }

    #[test]
    fn uncondition_bind_in_new_bytecode() {
        let condition_statement = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("wheatear"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(8),
            },
        };

        let rules =
            bind_rules::Ast { using: vec![], statements: vec![condition_statement.clone()] };
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("wheatear"),
            Symbol::Key("wheatear".to_string(), bind_library::ValueType::Number),
        );

        // Instructions should not contain unconditional bind.
        assert_eq!(
            compile_statements(rules.statements, &symbol_table, true).unwrap(),
            vec![SymbolicInstructionInfo {
                location: Some(AstLocation::ConditionStatement(condition_statement)),
                instruction: SymbolicInstruction::AbortIfNotEqual {
                    lhs: Symbol::Key("wheatear".to_string(), bind_library::ValueType::Number),
                    rhs: Symbol::NumberValue(8)
                }
            },]
        );
    }
}
