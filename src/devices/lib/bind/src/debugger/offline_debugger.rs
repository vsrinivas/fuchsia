// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bytecode_encoder::encode_v1::encode_symbol;
use crate::compiler::instruction::{InstructionDebug, RawAstLocation};
use crate::compiler::{self, BindRules, Symbol, SymbolTable, SymbolicInstruction};
use crate::debugger::device_specification::{DeviceSpecification, Property};
use crate::errors::UserError;
use crate::parser::bind_rules::{Condition, ConditionOp, Statement};
use crate::parser::common::{BindParserError, CompoundIdentifier, Span, Value};
use std::collections::{HashMap, HashSet};
use std::fmt;
use std::str::FromStr;
use thiserror::Error;

#[derive(Debug, Error, Clone, PartialEq)]
pub enum DebuggerError {
    ParserError(BindParserError),
    CompilerError(compiler::CompilerError),
    DuplicateKey(CompoundIdentifier),
    MissingLabel,
    NoOutcome,
    IncorrectAstLocation,
    InvalidAstLocation,
    UnknownKey(CompoundIdentifier),
    InvalidValueSymbol(Symbol),
}

impl fmt::Display for DebuggerError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

type DevicePropertyMap = HashMap<Symbol, DeviceValue>;

#[derive(Debug, PartialEq)]
struct DeviceValue {
    symbol: Option<Symbol>,
    identifier: Option<CompoundIdentifier>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum AstLocation<'a> {
    ConditionStatement(Statement<'a>),
    AcceptStatementValue { identifier: CompoundIdentifier, value: Value, span: Span<'a> },
    AcceptStatementFailure { identifier: CompoundIdentifier, symbol: Symbol, span: Span<'a> },
    IfCondition(Condition<'a>),
    FalseStatement(Statement<'a>),
}

impl<'a> AstLocation<'a> {
    pub fn to_instruction_debug(self) -> InstructionDebug {
        match self {
            AstLocation::ConditionStatement(statement) => InstructionDebug {
                line: statement.get_span().line,
                ast_location: RawAstLocation::ConditionStatement,
                extra: 0,
            },
            AstLocation::AcceptStatementValue { span, .. } => InstructionDebug {
                line: span.line,
                ast_location: RawAstLocation::AcceptStatementValue,
                extra: 0,
            },
            AstLocation::AcceptStatementFailure { span, symbol, .. } => InstructionDebug {
                line: span.line,
                ast_location: RawAstLocation::AcceptStatementFailure,
                extra: encode_symbol(symbol).unwrap_or(0),
            },
            AstLocation::IfCondition(condition) => InstructionDebug {
                line: condition.span.line,
                ast_location: RawAstLocation::IfCondition,
                extra: 0,
            },
            AstLocation::FalseStatement(statement) => InstructionDebug {
                line: statement.get_span().line,
                ast_location: RawAstLocation::FalseStatement,
                extra: 0,
            },
        }
    }
}

#[derive(Debug, PartialEq)]
enum DebuggerOutput<'a> {
    ConditionStatement {
        statement: &'a Statement<'a>,
        success: bool,
    },
    FalseStatement {
        statement: &'a Statement<'a>,
    },
    AcceptStatementSuccess {
        identifier: &'a CompoundIdentifier,
        value: &'a Value,
        value_symbol: &'a Symbol,
        span: &'a Span<'a>,
    },
    AcceptStatementFailure {
        identifier: &'a CompoundIdentifier,
        span: &'a Span<'a>,
    },
    IfCondition {
        condition: &'a Condition<'a>,
        success: bool,
    },
}

pub fn debug_from_str<'a>(
    bind_rules: &BindRules,
    device_file: &str,
) -> Result<bool, DebuggerError> {
    let device_specification =
        DeviceSpecification::from_str(device_file).map_err(DebuggerError::ParserError)?;

    debug_from_device_specification(bind_rules, device_specification)
}

pub fn debug_from_device_specification<'a>(
    bind_rules: &BindRules,
    device_specification: DeviceSpecification,
) -> Result<bool, DebuggerError> {
    let mut debugger = Debugger::new(&device_specification.properties, bind_rules)?;
    let binds = debugger.evaluate_bind_rules()?;
    debugger.log_output()?;
    Ok(binds)
}

struct Debugger<'a> {
    device_properties: DevicePropertyMap,
    bind_rules: &'a BindRules<'a>,
    output: Vec<DebuggerOutput<'a>>,
}

impl<'a> Debugger<'a> {
    fn new(properties: &[Property], bind_rules: &'a BindRules<'a>) -> Result<Self, DebuggerError> {
        let device_properties =
            Debugger::construct_property_map(properties, &bind_rules.symbol_table)?;
        let output = Vec::new();
        Ok(Debugger { device_properties, bind_rules, output })
    }

    /// Constructs a map of the device's properties. The keys are of type Symbol, and only keys
    /// which appear in the bind rules's symbol table are included. Any other keys in the device
    /// specification can be safely ignored, since the bind rules won't check their values. The
    /// values are a struct containing both a symbol and an identifier, to allow the debugger to
    /// output both the identifier and the literal value it corresponds to when printing values.
    /// Both these fields are optional. If the symbol is None, then the device value is an
    /// identifier not defined in the symbol table. If the identifier is None, then the device
    /// value is a literal.
    fn construct_property_map(
        properties: &[Property],
        symbol_table: &SymbolTable,
    ) -> Result<DevicePropertyMap, DebuggerError> {
        let mut property_map = HashMap::new();
        let mut keys_seen = HashSet::new();

        for Property { key, value } in properties {
            // Check for duplicate keys in the device specification. This is done using a separate
            // set since the property_map only contains keys which are present in the symbol table.
            if keys_seen.contains(key) {
                return Err(DebuggerError::DuplicateKey(key.clone()));
            }
            keys_seen.insert(key.clone());

            if let Some(key_symbol) = symbol_table.get(key) {
                let device_value = match value {
                    Value::NumericLiteral(n) => {
                        DeviceValue { symbol: Some(Symbol::NumberValue(*n)), identifier: None }
                    }
                    Value::StringLiteral(s) => DeviceValue {
                        symbol: Some(Symbol::StringValue(s.to_string())),
                        identifier: None,
                    },
                    Value::BoolLiteral(b) => {
                        DeviceValue { symbol: Some(Symbol::BoolValue(*b)), identifier: None }
                    }
                    Value::Identifier(identifier) => match symbol_table.get(identifier) {
                        Some(symbol) => DeviceValue {
                            symbol: Some(symbol.clone()),
                            identifier: Some(identifier.clone()),
                        },
                        None => DeviceValue { symbol: None, identifier: Some(identifier.clone()) },
                    },
                };

                property_map.insert(key_symbol.clone(), device_value);
            }
        }

        Ok(property_map)
    }

    fn evaluate_bind_rules(&mut self) -> Result<bool, DebuggerError> {
        let mut instructions = self.bind_rules.instructions.iter();

        while let Some(mut instruction) = instructions.next() {
            let mut jump_label = None;

            match &instruction.instruction {
                SymbolicInstruction::AbortIfEqual { lhs, rhs } => {
                    let aborts = self.device_property_matches(lhs, rhs);
                    self.output_abort_if(&instruction.location, aborts)?;
                    if aborts {
                        return Ok(false);
                    }
                }
                SymbolicInstruction::AbortIfNotEqual { lhs, rhs } => {
                    let aborts = !self.device_property_matches(lhs, rhs);
                    self.output_abort_if(&instruction.location, aborts)?;
                    if aborts {
                        return Ok(false);
                    }
                }
                SymbolicInstruction::Label(_label) => (),
                SymbolicInstruction::UnconditionalJump { label } => {
                    jump_label = Some(label);
                }
                SymbolicInstruction::JumpIfEqual { lhs, rhs, label } => {
                    let jump_succeeds = self.device_property_matches(lhs, rhs);
                    self.output_jump_if_equal(&instruction.location, rhs, jump_succeeds)?;
                    if jump_succeeds {
                        jump_label = Some(label);
                    }
                }
                SymbolicInstruction::JumpIfNotEqual { lhs, rhs, label } => {
                    let jump_succeeds = !self.device_property_matches(lhs, rhs);
                    self.output_jump_if_not_equal(&instruction.location, jump_succeeds)?;
                    if jump_succeeds {
                        jump_label = Some(label);
                    }
                }
                SymbolicInstruction::UnconditionalAbort => {
                    self.output_unconditional_abort(&instruction.location)?;
                    return Ok(false);
                }
                SymbolicInstruction::UnconditionalBind => return Ok(true),
            }

            if let Some(label) = jump_label {
                while instruction.instruction != SymbolicInstruction::Label(*label) {
                    instruction = instructions.next().ok_or(DebuggerError::MissingLabel)?;
                }
            }
        }

        Err(DebuggerError::NoOutcome)
    }

    fn device_property_matches(&self, lhs: &Symbol, rhs: &Symbol) -> bool {
        if let Some(DeviceValue { symbol: Some(value_symbol), identifier: _ }) =
            self.device_properties.get(lhs)
        {
            value_symbol == rhs
        } else {
            false
        }
    }

    fn output_abort_if(
        &mut self,
        location: &'a Option<AstLocation>,
        aborts: bool,
    ) -> Result<(), DebuggerError> {
        if let Some(AstLocation::ConditionStatement(statement)) = location {
            self.output.push(DebuggerOutput::ConditionStatement { statement, success: !aborts });
            Ok(())
        } else {
            Err(DebuggerError::IncorrectAstLocation)
        }
    }

    fn output_jump_if_equal(
        &mut self,
        location: &'a Option<AstLocation>,
        rhs: &'a Symbol,
        jump_succeeds: bool,
    ) -> Result<(), DebuggerError> {
        match location {
            Some(AstLocation::AcceptStatementValue { identifier, value, span }) => {
                if jump_succeeds {
                    self.output.push(DebuggerOutput::AcceptStatementSuccess {
                        identifier,
                        value,
                        value_symbol: rhs,
                        span,
                    });
                }
                Ok(())
            }
            Some(AstLocation::IfCondition(condition)) => {
                self.output
                    .push(DebuggerOutput::IfCondition { condition, success: !jump_succeeds });
                Ok(())
            }
            _ => Err(DebuggerError::IncorrectAstLocation),
        }
    }

    fn output_jump_if_not_equal(
        &mut self,
        location: &'a Option<AstLocation>,
        jump_succeeds: bool,
    ) -> Result<(), DebuggerError> {
        if let Some(AstLocation::IfCondition(condition)) = location {
            self.output.push(DebuggerOutput::IfCondition { condition, success: !jump_succeeds });
            Ok(())
        } else {
            Err(DebuggerError::IncorrectAstLocation)
        }
    }

    fn output_unconditional_abort(
        &mut self,
        location: &'a Option<AstLocation>,
    ) -> Result<(), DebuggerError> {
        match location {
            Some(AstLocation::FalseStatement(statement)) => {
                self.output.push(DebuggerOutput::FalseStatement { statement });
                Ok(())
            }
            Some(AstLocation::AcceptStatementFailure { identifier, span, symbol: _ }) => {
                self.output.push(DebuggerOutput::AcceptStatementFailure { identifier, span });
                Ok(())
            }
            _ => Err(DebuggerError::IncorrectAstLocation),
        }
    }

    fn log_output(&self) -> Result<(), DebuggerError> {
        for output in &self.output {
            match output {
                DebuggerOutput::ConditionStatement { statement, success } => {
                    self.log_condition_statement(statement, *success)?;
                }
                DebuggerOutput::FalseStatement { statement } => self.log_abort_statement(statement),
                DebuggerOutput::AcceptStatementSuccess {
                    identifier,
                    value,
                    value_symbol,
                    span,
                } => self.log_accept_statement_success(identifier, value, value_symbol, span)?,
                DebuggerOutput::AcceptStatementFailure { identifier, span } => {
                    self.log_accept_statement_failure(identifier, span)?;
                }
                DebuggerOutput::IfCondition { condition, success } => {
                    self.log_if_condition(condition, *success)?;
                }
            }
        }
        Ok(())
    }

    fn log_condition_statement(
        &self,
        statement: &Statement,
        success: bool,
    ) -> Result<(), DebuggerError> {
        if let Statement::ConditionStatement { span, condition: Condition { lhs, op, .. } } =
            statement
        {
            let outcome_string = if success { "succeeded" } else { "failed" };
            println!(
                "Line {}: Condition statement {}: {}",
                span.line, outcome_string, span.fragment
            );

            if condition_needs_actual_value(success, op) {
                println!("\t{}", self.actual_value_string(lhs)?)
            }

            Ok(())
        } else {
            Err(DebuggerError::InvalidAstLocation)
        }
    }

    fn log_abort_statement(&self, statement: &Statement) {
        if let Statement::False { span } = statement {
            println!("Line {}: Abort statement reached.", span.line);
        }
    }

    fn log_accept_statement_success(
        &self,
        identifier: &CompoundIdentifier,
        value: &Value,
        value_symbol: &Symbol,
        span: &Span,
    ) -> Result<(), DebuggerError> {
        // Get the value identifier from the accept statement (or None for a literal value).
        let value_identifier_prog =
            if let Value::Identifier(identifier) = value { Some(identifier) } else { None };

        // Get the value identifier from the device specification (or None for a literal value).
        let key_symbol = self
            .bind_rules
            .symbol_table
            .get(identifier)
            .ok_or(DebuggerError::UnknownKey(identifier.clone()))?;
        let DeviceValue { symbol: _, identifier: value_identifier_device } = self
            .device_properties
            .get(key_symbol)
            .expect("Accept statement succeeded so device must have this key.");

        let value_literal = value_symbol_string(value_symbol)?;

        let value_string = match (value_identifier_prog, value_identifier_device) {
            (Some(identifier_prog), Some(identifier_value)) => {
                if identifier_prog == identifier_value {
                    format!("`{}` [{}]", identifier_prog, value_literal)
                } else {
                    format!("`{}` (`{}`) [{}]", identifier_prog, identifier_value, value_literal)
                }
            }
            (Some(identifier_prog), None) => format!("`{}` [{}]", identifier_prog, value_literal),
            (None, Some(identifier_value)) => format!("`{}` [{}]", identifier_value, value_literal),
            (None, None) => format!("{}", value_literal),
        };

        println!(
            "Line {}: Accept statement succeeded.\n\tValue of `{}` was {}.",
            span.line, identifier, value_string
        );

        Ok(())
    }

    fn log_accept_statement_failure(
        &self,
        identifier: &CompoundIdentifier,
        span: &Span,
    ) -> Result<(), DebuggerError> {
        println!(
            "Line {}: Accept statement failed.\n\t{}",
            span.line,
            self.actual_value_string(identifier)?
        );

        Ok(())
    }

    fn log_if_condition(&self, condition: &Condition, success: bool) -> Result<(), DebuggerError> {
        let Condition { span, lhs, op, rhs: _ } = condition;

        let outcome_string = if success { "succeeded" } else { "failed" };
        println!(
            "Line {}: If statement condition {}: {}",
            span.line, outcome_string, span.fragment
        );

        if condition_needs_actual_value(success, op) {
            println!("\t{}", self.actual_value_string(lhs)?)
        }

        Ok(())
    }

    fn actual_value_string(
        &self,
        key_identifier: &CompoundIdentifier,
    ) -> Result<String, DebuggerError> {
        let key_symbol = self
            .bind_rules
            .symbol_table
            .get(key_identifier)
            .ok_or(DebuggerError::UnknownKey(key_identifier.clone()))?;
        let DeviceValue { symbol: value_symbol, identifier: value_identifier } = self
            .device_properties
            .get(key_symbol)
            .unwrap_or(&DeviceValue { symbol: None, identifier: None });

        Ok(match (value_symbol, value_identifier) {
            (Some(symbol), Some(identifier)) => format!(
                "Actual value of `{}` was `{}` [{}].",
                key_identifier,
                identifier,
                value_symbol_string(symbol)?
            ),
            (Some(symbol), None) => format!(
                "Actual value of `{}` was literal {}.",
                key_identifier,
                value_symbol_string(symbol)?
            ),
            (None, Some(identifier)) => {
                format!("Actual value of `{}` was `{}`.", key_identifier, identifier)
            }
            (None, None) => format!("Device had no value for `{}`.", key_identifier),
        })
    }
}

fn condition_needs_actual_value(success: bool, op: &ConditionOp) -> bool {
    match op {
        ConditionOp::Equals => !success,
        ConditionOp::NotEquals => success,
    }
}

fn value_symbol_string(symbol: &Symbol) -> Result<String, DebuggerError> {
    match symbol {
        Symbol::DeprecatedKey(..) => Err(DebuggerError::InvalidValueSymbol(symbol.clone())),
        Symbol::Key(..) => Err(DebuggerError::InvalidValueSymbol(symbol.clone())),
        Symbol::NumberValue(n) => Ok(format!("0x{:x}", n)),
        Symbol::StringValue(s) => Ok(format!("\"{}\"", s)),
        Symbol::BoolValue(b) => Ok(b.to_string()),
        Symbol::EnumValue(s) => Ok(format!("\"{}\"", s)),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;
    use crate::parser::bind_library;
    use crate::parser::bind_rules::{Condition, ConditionOp, Statement};
    use crate::parser::common::{CompoundIdentifier, Span};

    fn compile<'a>(symbol_table: SymbolTable, statements: Vec<Statement<'a>>) -> BindRules<'a> {
        let instructions = compiler::compile_statements(statements, &symbol_table, false).unwrap();
        BindRules {
            instructions: instructions,
            symbol_table: symbol_table,
            use_new_bytecode: false,
        }
    }

    #[test]
    fn duplicate_key() {
        let symbol_table = HashMap::new();
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) },
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(5) },
        ];
        assert_eq!(
            Debugger::construct_property_map(&properties, &symbol_table),
            Err(DebuggerError::DuplicateKey(make_identifier!("abc")))
        );
    }

    #[test]
    fn condition_equals() {
        let statement = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(42),
            },
        };
        let statements = vec![statement.clone()];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );
        let bind_rules = compile(symbol_table.clone(), statements);

        // Binds when the device has the correct property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::ConditionStatement { statement: &statement, success: true }]
        );

        // Binds when other properties are present as well.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) },
            Property { key: make_identifier!("xyz"), value: Value::BoolLiteral(true) },
        ];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::ConditionStatement { statement: &statement, success: true }]
        );

        // Doesn't bind when the device has the wrong value for the property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(5) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::ConditionStatement { statement: &statement, success: false }]
        );

        // Doesn't bind when the property is not present in the device.
        let properties = Vec::new();
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::ConditionStatement { statement: &statement, success: false }]
        );
    }

    #[test]
    fn condition_not_equals() {
        let statement = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::NotEquals,
                rhs: Value::NumericLiteral(42),
            },
        };
        let statements = vec![statement.clone()];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );
        let bind_rules = compile(symbol_table, statements);

        // Binds when the device has a different value for the property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(5) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::ConditionStatement { statement: &statement, success: true }]
        );

        // Binds when the property is not present in the device.
        let properties = Vec::new();
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::ConditionStatement { statement: &statement, success: true }]
        );

        // Doesn't bind when the device has the property in the condition statement.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::ConditionStatement { statement: &statement, success: false }]
        );
    }

    #[test]
    fn accept() {
        let statements = vec![Statement::Accept {
            span: Span::new(),
            identifier: make_identifier!("abc"),
            values: vec![Value::NumericLiteral(42), Value::NumericLiteral(314)],
        }];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        let bind_rules = compile(symbol_table, statements);

        // Binds when the device has one of the accepted values for the property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::AcceptStatementSuccess {
                identifier: &make_identifier!("abc"),
                value: &Value::NumericLiteral(42),
                value_symbol: &Symbol::NumberValue(42),
                span: &Span::new(),
            }]
        );

        // Doesn't bind when the device has a different value for the property.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(5) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::AcceptStatementFailure {
                identifier: &make_identifier!("abc"),
                span: &Span::new(),
            }]
        );

        // Doesn't bind when the device is missing the property.
        let properties = Vec::new();
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::AcceptStatementFailure {
                identifier: &make_identifier!("abc"),
                span: &Span::new(),
            }]
        );
    }

    #[test]
    fn if_else() {
        /*
        if abc == 1 {
            xyz == 1;
        } else if abc == 2{
            xyz == 2;
        } else {
            xyz == 3;
        }
        */

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
                lhs: make_identifier!("xyz"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(1),
            },
        };
        let statement2 = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("xyz"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(2),
            },
        };
        let statement3 = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("xyz"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(3),
            },
        };

        let statements = vec![Statement::If {
            span: Span::new(),
            blocks: vec![
                (condition1.clone(), vec![statement1.clone()]),
                (condition2.clone(), vec![statement2.clone()]),
            ],
            else_block: vec![statement3.clone()],
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

        let bind_rules = compile(symbol_table, statements);

        // Binds when the if clause is satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(1) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(1) },
        ];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition1, success: true },
                DebuggerOutput::ConditionStatement { statement: &statement1, success: true }
            ]
        );

        // Binds when the if else clause is satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(2) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(2) },
        ];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition1, success: false },
                DebuggerOutput::IfCondition { condition: &condition2, success: true },
                DebuggerOutput::ConditionStatement { statement: &statement2, success: true }
            ]
        );

        // Binds when the else clause is satisfied.
        let properties =
            vec![Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(3) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition1, success: false },
                DebuggerOutput::IfCondition { condition: &condition2, success: false },
                DebuggerOutput::ConditionStatement { statement: &statement3, success: true }
            ]
        );

        // Doesn't bind when the device has incorrect values for the properties.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(42) },
        ];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition1, success: false },
                DebuggerOutput::IfCondition { condition: &condition2, success: false },
                DebuggerOutput::ConditionStatement { statement: &statement3, success: false }
            ]
        );

        // Doesn't bind when the properties are missing in the device.
        let properties = Vec::new();
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition1, success: false },
                DebuggerOutput::IfCondition { condition: &condition2, success: false },
                DebuggerOutput::ConditionStatement { statement: &statement3, success: false }
            ]
        );
    }

    #[test]
    fn abort() {
        let abort_statement = Statement::False { span: Span::new() };
        let statements = vec![abort_statement.clone()];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        let bind_rules = compile(symbol_table, statements);

        // Doesn't bind when abort statement is present.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![DebuggerOutput::FalseStatement { statement: &abort_statement }]
        );
    }

    #[test]
    fn supports_all_value_types() {
        let statements = vec![
            Statement::ConditionStatement {
                span: Span::new(),
                condition: Condition {
                    span: Span::new(),
                    lhs: make_identifier!("a"),
                    op: ConditionOp::Equals,
                    rhs: Value::NumericLiteral(42),
                },
            },
            Statement::ConditionStatement {
                span: Span::new(),
                condition: Condition {
                    span: Span::new(),
                    lhs: make_identifier!("b"),
                    op: ConditionOp::Equals,
                    rhs: Value::BoolLiteral(false),
                },
            },
            Statement::ConditionStatement {
                span: Span::new(),
                condition: Condition {
                    span: Span::new(),
                    lhs: make_identifier!("c"),
                    op: ConditionOp::Equals,
                    rhs: Value::StringLiteral("string".to_string()),
                },
            },
            Statement::ConditionStatement {
                span: Span::new(),
                condition: Condition {
                    span: Span::new(),
                    lhs: make_identifier!("d"),
                    op: ConditionOp::Equals,
                    rhs: Value::Identifier(make_identifier!("VALUE")),
                },
            },
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

        let bind_rules = compile(symbol_table, statements);

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
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
    }

    #[test]
    fn full_rules() {
        /*
        if abc == 42 {
            abort;
        } else {
            accept xyz {1, 2};
            pqr != true;
        }
        */

        let condition = Condition {
            span: Span::new(),
            lhs: make_identifier!("abc"),
            op: ConditionOp::Equals,
            rhs: Value::NumericLiteral(42),
        };
        let abort_statement = Statement::False { span: Span::new() };
        let condition_statement = Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("pqr"),
                op: ConditionOp::NotEquals,
                rhs: Value::BoolLiteral(true),
            },
        };

        let statements = vec![Statement::If {
            span: Span::new(),
            blocks: vec![(condition.clone(), vec![abort_statement.clone()])],
            else_block: vec![
                Statement::Accept {
                    span: Span::new(),
                    identifier: make_identifier!("xyz"),
                    values: vec![Value::NumericLiteral(1), Value::NumericLiteral(2)],
                },
                condition_statement.clone(),
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
        let bind_rules = compile(symbol_table, statements);

        // Aborts because if condition is true.
        let properties =
            vec![Property { key: make_identifier!("abc"), value: Value::NumericLiteral(42) }];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition, success: true },
                DebuggerOutput::FalseStatement { statement: &abort_statement }
            ]
        );

        // Binds because all statements inside else block are satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(43) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(1) },
        ];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition, success: false },
                DebuggerOutput::AcceptStatementSuccess {
                    identifier: &make_identifier!("xyz"),
                    value: &Value::NumericLiteral(1),
                    value_symbol: &Symbol::NumberValue(1),
                    span: &Span::new(),
                },
                DebuggerOutput::ConditionStatement {
                    statement: &condition_statement,
                    success: true
                }
            ]
        );

        // Doesn't bind because accept statement is not satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(43) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(3) },
        ];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition, success: false },
                DebuggerOutput::AcceptStatementFailure {
                    identifier: &make_identifier!("xyz"),
                    span: &Span::new(),
                },
            ]
        );

        // Doesn't bind because condition statement is not satisfied.
        let properties = vec![
            Property { key: make_identifier!("abc"), value: Value::NumericLiteral(43) },
            Property { key: make_identifier!("xyz"), value: Value::NumericLiteral(1) },
            Property { key: make_identifier!("pqr"), value: Value::BoolLiteral(true) },
        ];
        let mut debugger = Debugger::new(&properties, &bind_rules).unwrap();
        assert!(!debugger.evaluate_bind_rules().unwrap());
        assert_eq!(
            debugger.output,
            vec![
                DebuggerOutput::IfCondition { condition: &condition, success: false },
                DebuggerOutput::AcceptStatementSuccess {
                    identifier: &make_identifier!("xyz"),
                    value: &Value::NumericLiteral(1),
                    value_symbol: &Symbol::NumberValue(1),
                    span: &Span::new(),
                },
                DebuggerOutput::ConditionStatement {
                    statement: &condition_statement,
                    success: false
                }
            ]
        );
    }
}
