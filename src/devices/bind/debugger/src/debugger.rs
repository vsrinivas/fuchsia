// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler;
use crate::ddk_bind_constants::{BIND_AUTOBIND, BIND_FLAGS, BIND_PROTOCOL};
use crate::encode_bind_program_v1::{RawCondition, RawInstruction, RawOp};
use crate::errors::UserError;
use crate::instruction::{DeviceProperty, RawAstLocation};
use num_traits::FromPrimitive;
use std::collections::HashMap;
use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub enum DebuggerError {
    BindFlagsNotSupported,
    InvalidCondition(u32),
    IncorrectCondition,
    InvalidOperation(u32),
    InvalidAstLocation(u32),
    IncorrectAstLocation,
    MissingLabel,
    MissingBindProtocol,
    NoOutcome,
    DuplicateKey(u32),
    InvalidDeprecatedKey(u32),
}

impl fmt::Display for DebuggerError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

type DevicePropertyMap = HashMap<u32, u32>;

#[derive(Debug, PartialEq)]
enum DebuggerOutput {
    ConditionStatement { key: u32, condition: RawCondition, success: bool, line: u32 },
    AbortStatement { line: u32 },
    AcceptStatementSuccess { key: u32, value: u32, line: u32 },
    AcceptStatementFailure { key: u32, line: u32 },
    IfCondition { key: u32, condition: RawCondition, success: bool, line: u32 },
}

// TODO(fxb:67441): Support the new bytecode format in the debugger.
pub fn debug(
    instructions: &[RawInstruction<[u32; 3]>],
    properties: &[DeviceProperty],
) -> Result<bool, DebuggerError> {
    let mut debugger = Debugger::new(instructions, properties)?;
    let binds = debugger.evaluate_bind_program()?;
    debugger.log_output()?;
    Ok(binds)
}

struct Debugger<'a> {
    instructions: &'a [RawInstruction<[u32; 3]>],
    properties: DevicePropertyMap,
    output: Vec<DebuggerOutput>,
    deprecated_key_identifiers: HashMap<u32, String>,
}

impl<'a> Debugger<'a> {
    pub fn new(
        instructions: &'a [RawInstruction<[u32; 3]>],
        properties: &[DeviceProperty],
    ) -> Result<Self, DebuggerError> {
        let properties = Debugger::construct_property_map(properties)?;
        let output = Vec::new();
        let deprecated_key_identifiers = compiler::get_deprecated_key_identifiers();
        Ok(Debugger { instructions, properties, output, deprecated_key_identifiers })
    }

    fn construct_property_map(
        properties: &[DeviceProperty],
    ) -> Result<DevicePropertyMap, DebuggerError> {
        let mut property_map = HashMap::new();
        for DeviceProperty { key, value } in properties {
            if property_map.contains_key(key) {
                return Err(DebuggerError::DuplicateKey(*key));
            }
            property_map.insert(*key, *value);
        }
        Ok(property_map)
    }

    fn key_string(&self, key: u32) -> Result<String, DebuggerError> {
        match self.deprecated_key_identifiers.get(&key) {
            Some(identifier) => Ok(format!("`{}` [{:#06x}]", identifier, key)),
            None => Err(DebuggerError::InvalidDeprecatedKey(key)),
        }
    }

    fn get_device_property(&self, key: u32) -> Result<u32, DebuggerError> {
        let value = self.properties.get(&key);
        if let Some(value) = value {
            return Ok(*value);
        }

        // TODO(fxbug.dev/45663): The behavior of setting missing properties to 0 is implemented to be
        // consistent with binding.cc. This behavior should eventually be changed to deal with
        // missing properties in a better way.
        match key {
            BIND_PROTOCOL => return Err(DebuggerError::MissingBindProtocol),
            BIND_AUTOBIND => {
                println!(
                    "WARNING: Driver has BI_ABORT_IF_AUTOBIND. \
                    This bind program will fail in autobind contexts."
                );
                // Set autobind = false, since the debugger is always run with a specific driver.
                Ok(0)
            }
            _ => {
                println!(
                    "WARNING: Device has no value for {}. The value will be set to 0.",
                    self.key_string(key)?
                );
                Ok(0)
            }
        }
    }

    pub fn evaluate_bind_program(&mut self) -> Result<bool, DebuggerError> {
        let mut instructions = self.instructions.iter();

        while let Some(mut instruction) = instructions.next() {
            let condition = FromPrimitive::from_u32(instruction.condition())
                .ok_or(DebuggerError::InvalidCondition(instruction.condition()))?;
            let operation = FromPrimitive::from_u32(instruction.operation())
                .ok_or(DebuggerError::InvalidOperation(instruction.operation()))?;

            let condition_succeeds = if condition == RawCondition::Always {
                true
            } else {
                let key = instruction.parameter_b();
                if key == BIND_FLAGS {
                    return Err(DebuggerError::BindFlagsNotSupported);
                }

                let device_value = self.get_device_property(key)?;

                match condition {
                    RawCondition::Equal => device_value == instruction.value(),
                    RawCondition::NotEqual => device_value != instruction.value(),
                    RawCondition::Always => unreachable!(),
                }
            };

            self.output_instruction(instruction, condition_succeeds)?;

            if !condition_succeeds {
                continue;
            }

            match operation {
                RawOp::Abort => return Ok(false),
                RawOp::Match => return Ok(true),
                RawOp::Goto => {
                    let label = instruction.parameter_a();
                    while !(FromPrimitive::from_u32(instruction.operation()) == Some(RawOp::Label)
                        && instruction.parameter_a() == label)
                    {
                        instruction = instructions.next().ok_or(DebuggerError::MissingLabel)?;
                    }
                }
                RawOp::Label => (),
            }
        }

        Err(DebuggerError::NoOutcome)
    }

    fn output_instruction(
        &mut self,
        instruction: &RawInstruction<[u32; 3]>,
        condition_succeeds: bool,
    ) -> Result<(), DebuggerError> {
        let condition = FromPrimitive::from_u32(instruction.condition())
            .ok_or(DebuggerError::InvalidCondition(instruction.condition()))?;
        let operation = FromPrimitive::from_u32(instruction.operation())
            .ok_or(DebuggerError::InvalidOperation(instruction.operation()))?;
        let ast_location = FromPrimitive::from_u32(instruction.ast_location())
            .ok_or(DebuggerError::InvalidAstLocation(instruction.ast_location()))?;

        match (operation, condition) {
            (RawOp::Abort, RawCondition::Equal) | (RawOp::Abort, RawCondition::NotEqual) => {
                if ast_location != RawAstLocation::ConditionStatement {
                    return Err(DebuggerError::IncorrectAstLocation);
                }
                // The abort instruction came from a condition statement. An equality condition
                // statement is compiled to AbortIfNotEqual and vice versa, so the condition needs
                // to be flipped. Also, the condition was successful if the instruction doesn't
                // abort, so the `success` value needs to be flipped.
                self.output.push(DebuggerOutput::ConditionStatement {
                    key: instruction.parameter_b(),
                    condition: match condition {
                        RawCondition::Equal => RawCondition::NotEqual,
                        RawCondition::NotEqual => RawCondition::Equal,
                        _ => return Err(DebuggerError::IncorrectCondition),
                    },
                    success: !condition_succeeds,
                    line: instruction.line(),
                });
            }
            (RawOp::Abort, RawCondition::Always) => match ast_location {
                RawAstLocation::AcceptStatementFailure => {
                    // The abort instruction came from the end of an accept statement, meaning that
                    // the accept statement failed.
                    self.output.push(DebuggerOutput::AcceptStatementFailure {
                        key: instruction.extra(),
                        line: instruction.line(),
                    })
                }
                RawAstLocation::AbortStatement => {
                    self.output.push(DebuggerOutput::AbortStatement { line: instruction.line() })
                }
                _ => return Err(DebuggerError::IncorrectAstLocation),
            },
            (RawOp::Goto, RawCondition::Equal) => match ast_location {
                RawAstLocation::AcceptStatementValue => {
                    if condition_succeeds {
                        // The goto instruciton came from one of the values in an accept statement.
                        // The fact that the jump succeeded means that the device had this value, so
                        // the accept statement was satisfied.
                        self.output.push(DebuggerOutput::AcceptStatementSuccess {
                            key: instruction.parameter_b(),
                            value: instruction.value(),
                            line: instruction.line(),
                        });
                    }
                }
                RawAstLocation::IfCondition => {
                    // The goto instruction came from an inequality if statement condition. The
                    // condition was satisfied if the jump doesn't succeed, so the `success` value
                    // needs to be flipped.
                    self.output.push(DebuggerOutput::IfCondition {
                        key: instruction.parameter_b(),
                        condition: RawCondition::NotEqual,
                        success: !condition_succeeds,
                        line: instruction.line(),
                    })
                }
                _ => return Err(DebuggerError::IncorrectAstLocation),
            },
            (RawOp::Goto, RawCondition::NotEqual) => {
                if ast_location != RawAstLocation::IfCondition {
                    return Err(DebuggerError::IncorrectAstLocation);
                }
                // The goto instruction came from an equality if statement condition. The
                // condition was satisfied if the jump doesn't succeed, so the `success` value
                // needs to be flipped.
                self.output.push(DebuggerOutput::IfCondition {
                    key: instruction.parameter_b(),
                    condition: RawCondition::Equal,
                    success: !condition_succeeds,
                    line: instruction.line(),
                });
            }
            _ => (),
        }
        Ok(())
    }

    fn log_output(&self) -> Result<(), DebuggerError> {
        for output in &self.output {
            match output {
                DebuggerOutput::ConditionStatement { key, condition, success, line } => {
                    self.log_condition_statement(*key, *condition, *success, *line)?;
                }
                DebuggerOutput::AbortStatement { line } => self.log_abort_statement(*line),
                DebuggerOutput::AcceptStatementSuccess { key, value, line } => {
                    self.log_accept_statement_success(*key, *value, *line)?
                }
                DebuggerOutput::AcceptStatementFailure { key, line } => {
                    self.log_accept_statement_failure(*key, *line)?
                }
                DebuggerOutput::IfCondition { key, condition, success, line } => {
                    self.log_if_condition(*key, *condition, *success, *line)?;
                }
            }
        }
        Ok(())
    }

    fn log_condition_statement(
        &self,
        key: u32,
        condition: RawCondition,
        success: bool,
        line: u32,
    ) -> Result<(), DebuggerError> {
        let outcome_string = if success { "succeeded" } else { "failed" };
        println!("Line {}: Condition statement {}.", line, outcome_string);

        if condition_needs_actual_value(success, condition)? {
            println!("\t{}", self.actual_value_string(key)?)
        }

        Ok(())
    }

    fn log_abort_statement(&self, line: u32) {
        println!("Line {}: Abort statement reached.", line);
    }

    fn log_accept_statement_success(
        &self,
        key: u32,
        value: u32,
        line: u32,
    ) -> Result<(), DebuggerError> {
        println!(
            "Line {}: Accept statement succeeded.\n\tThe value of {} was {:#010x}.",
            line,
            self.key_string(key)?,
            value
        );

        Ok(())
    }

    fn log_accept_statement_failure(&self, key: u32, line: u32) -> Result<(), DebuggerError> {
        println!("Line {}: Accept statement failed.\n\t{}", line, self.actual_value_string(key)?);
        Ok(())
    }

    fn log_if_condition(
        &self,
        key: u32,
        condition: RawCondition,
        success: bool,
        line: u32,
    ) -> Result<(), DebuggerError> {
        let outcome_string = if success { "succeeded" } else { "failed" };
        println!("Line {}: If statement condition {}.", line, outcome_string);

        if condition_needs_actual_value(success, condition)? {
            println!("\t{}", self.actual_value_string(key)?)
        }

        Ok(())
    }

    fn actual_value_string(&self, key: u32) -> Result<String, DebuggerError> {
        let value = self.properties.get(&key);

        Ok(match value {
            Some(value) => {
                format!("Actual value of {} was {:#010x}.", self.key_string(key)?, value)
            }
            None => format!("Device had no value for {}.", self.key_string(key)?),
        })
    }
}

fn condition_needs_actual_value(
    success: bool,
    condition: RawCondition,
) -> Result<bool, DebuggerError> {
    match condition {
        RawCondition::Equal => Ok(!success),
        RawCondition::NotEqual => Ok(success),
        RawCondition::Always => Err(DebuggerError::IncorrectCondition),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bind_program::{Condition, ConditionOp, Statement};
    use crate::compiler::{self, Symbol, SymbolTable};
    use crate::encode_bind_program_v1::encode_instruction;
    use crate::instruction::{self, Instruction};
    use crate::make_identifier;
    use crate::parser_common::{CompoundIdentifier, Span, Value};

    fn compile_to_raw(
        statements: Vec<Statement>,
        symbol_table: SymbolTable,
    ) -> Vec<RawInstruction<[u32; 3]>> {
        compiler::compile_statements(statements, symbol_table)
            .unwrap()
            .instructions
            .into_iter()
            .map(|symbolic| encode_instruction(symbolic.to_instruction()))
            .collect()
    }

    fn span_with_line(line: u32) -> Span<'static> {
        let mut span = Span::new();
        span.line = line;
        span
    }

    #[test]
    fn autobind() {
        // Autobind is false (BIND_AUTOBIND has the value 0).
        let instructions = vec![
            RawInstruction::from(Instruction::Match(instruction::Condition::Equal(
                Symbol::NumberValue(BIND_AUTOBIND.into()),
                Symbol::NumberValue(0),
            ))),
            RawInstruction::from(Instruction::Abort(instruction::Condition::Always)),
        ];
        let properties = Vec::new();
        assert_eq!(debug(&instructions, &properties), Ok(true));
    }

    #[test]
    fn bind_flags_not_supported() {
        let instructions =
            vec![RawInstruction::from(Instruction::Abort(instruction::Condition::Equal(
                Symbol::NumberValue(BIND_FLAGS.into()),
                Symbol::NumberValue(0),
            )))];
        let properties = Vec::new();
        assert_eq!(debug(&instructions, &properties), Err(DebuggerError::BindFlagsNotSupported));
    }

    #[test]
    fn missing_bind_protocol() {
        let instructions =
            vec![RawInstruction::from(Instruction::Abort(instruction::Condition::Equal(
                Symbol::NumberValue(BIND_PROTOCOL.into()),
                Symbol::NumberValue(5),
            )))];
        let properties = Vec::new();
        assert_eq!(debug(&instructions, &properties), Err(DebuggerError::MissingBindProtocol));
    }

    #[test]
    fn duplicate_key() {
        let instructions = Vec::new();
        let properties = vec![
            DeviceProperty { key: 0x0100, value: 42 },
            DeviceProperty { key: 0x0100, value: 5 },
        ];
        assert_eq!(debug(&instructions, &properties), Err(DebuggerError::DuplicateKey(0x0100)));
    }

    #[test]
    fn default_value_zero() {
        // When the device doesn't have the property, its value is set to 0.
        let statements = vec![Statement::ConditionStatement {
            span: Span::new(),
            condition: Condition {
                span: Span::new(),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(0),
            },
        }];
        let mut symbol_table = HashMap::new();
        symbol_table.insert(make_identifier!("abc"), Symbol::DeprecatedKey(0x0100));
        let raw_instructions = compile_to_raw(statements, symbol_table);
        let properties = Vec::new();
        assert_eq!(debug(&raw_instructions, &properties), Ok(true));
    }

    mod condition_equals {
        use super::*;

        fn condition_equals_instructions() -> Vec<RawInstruction<[u32; 3]>> {
            let statements = vec![Statement::ConditionStatement {
                span: span_with_line(7),
                condition: Condition {
                    span: span_with_line(7),
                    lhs: make_identifier!("abc"),
                    op: ConditionOp::Equals,
                    rhs: Value::NumericLiteral(42),
                },
            }];
            let mut symbol_table = HashMap::new();
            symbol_table.insert(make_identifier!("abc"), Symbol::DeprecatedKey(0x0100));
            compile_to_raw(statements, symbol_table)
        }

        #[test]
        fn correct_value() {
            // Binds when the device has the correct value for the property.
            let raw_instructions = condition_equals_instructions();
            let properties = vec![DeviceProperty { key: 0x0100, value: 42 }];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::ConditionStatement {
                    key: 0x0100,
                    condition: RawCondition::Equal,
                    success: true,
                    line: 7
                }]
            );
        }

        #[test]
        fn wrong_value() {
            // Doesn't bind when the device has the wrong value for the property.
            let raw_instructions = condition_equals_instructions();
            let properties = vec![DeviceProperty { key: 0x0100, value: 5 }];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::ConditionStatement {
                    key: 0x0100,
                    condition: RawCondition::Equal,
                    success: false,
                    line: 7
                }]
            );
        }

        #[test]
        fn missing_value() {
            // Doesn't bind when the property is not present in the device.
            let raw_instructions = condition_equals_instructions();
            let properties = Vec::new();
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::ConditionStatement {
                    key: 0x0100,
                    condition: RawCondition::Equal,
                    success: false,
                    line: 7
                }]
            );
        }
    }

    mod condition_not_equals {
        use super::*;

        fn condition_not_equals_instructions() -> Vec<RawInstruction<[u32; 3]>> {
            let statements = vec![Statement::ConditionStatement {
                span: span_with_line(7),
                condition: Condition {
                    span: span_with_line(7),
                    lhs: make_identifier!("abc"),
                    op: ConditionOp::NotEquals,
                    rhs: Value::NumericLiteral(42),
                },
            }];
            let mut symbol_table = HashMap::new();
            symbol_table.insert(make_identifier!("abc"), Symbol::DeprecatedKey(0x0100));
            compile_to_raw(statements, symbol_table)
        }

        #[test]
        fn different_value() {
            // Binds when the device has a different value for the property.
            let raw_instructions = condition_not_equals_instructions();
            let properties = vec![DeviceProperty { key: 0x0100, value: 5 }];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::ConditionStatement {
                    key: 0x0100,
                    condition: RawCondition::NotEqual,
                    success: true,
                    line: 7
                }]
            );
        }

        #[test]
        fn missing_value() {
            // Binds when the property is not present in the device.
            let raw_instructions = condition_not_equals_instructions();
            let properties = Vec::new();
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::ConditionStatement {
                    key: 0x0100,
                    condition: RawCondition::NotEqual,
                    success: true,
                    line: 7
                }]
            );
        }

        #[test]
        fn same_value() {
            // Doesn't bind when the device has the property in the condition statement.
            let raw_instructions = condition_not_equals_instructions();
            let properties = vec![DeviceProperty { key: 0x0100, value: 42 }];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::ConditionStatement {
                    key: 0x0100,
                    condition: RawCondition::NotEqual,
                    success: false,
                    line: 7
                }]
            );
        }
    }

    mod accept {
        use super::*;

        fn accept_instructions() -> Vec<RawInstruction<[u32; 3]>> {
            let statements = vec![Statement::Accept {
                span: span_with_line(7),
                identifier: make_identifier!("abc"),
                values: vec![Value::NumericLiteral(42), Value::NumericLiteral(314)],
            }];
            let mut symbol_table = HashMap::new();
            symbol_table.insert(make_identifier!("abc"), Symbol::DeprecatedKey(0x0100));

            compile_to_raw(statements, symbol_table)
        }

        #[test]
        fn accepted_value() {
            // Binds when the device has one of the accepted values for the property.
            let raw_instructions = accept_instructions();
            let properties = vec![DeviceProperty { key: 0x0100, value: 42 }];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::AcceptStatementSuccess { key: 0x0100, value: 42, line: 7 }]
            );
        }

        #[test]
        fn different_value() {
            // Doesn't bind when the device has a different value for the property.
            let raw_instructions = accept_instructions();
            let properties = vec![DeviceProperty { key: 0x0100, value: 5 }];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::AcceptStatementFailure { key: 0x0100, line: 7 }]
            );
        }

        #[test]
        fn missing_value() {
            // Doesn't bind when the device is missing the property.
            let raw_instructions = accept_instructions();
            let properties = Vec::new();
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![DebuggerOutput::AcceptStatementFailure { key: 0x0100, line: 7 }]
            );
        }
    }

    mod if_else {
        use super::*;

        fn if_else_instructions() -> Vec<RawInstruction<[u32; 3]>> {
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
                span: span_with_line(1),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(1),
            };
            let statement1 = Statement::ConditionStatement {
                span: span_with_line(2),
                condition: Condition {
                    span: span_with_line(2),
                    lhs: make_identifier!("xyz"),
                    op: ConditionOp::Equals,
                    rhs: Value::NumericLiteral(1),
                },
            };
            let condition2 = Condition {
                span: span_with_line(3),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(2),
            };
            let statement2 = Statement::ConditionStatement {
                span: span_with_line(4),
                condition: Condition {
                    span: span_with_line(1),
                    lhs: make_identifier!("xyz"),
                    op: ConditionOp::Equals,
                    rhs: Value::NumericLiteral(2),
                },
            };
            let statement3 = Statement::ConditionStatement {
                span: span_with_line(6),
                condition: Condition {
                    span: span_with_line(6),
                    lhs: make_identifier!("xyz"),
                    op: ConditionOp::Equals,
                    rhs: Value::NumericLiteral(3),
                },
            };

            let statements = vec![Statement::If {
                span: span_with_line(1),
                blocks: vec![
                    (condition1.clone(), vec![statement1.clone()]),
                    (condition2.clone(), vec![statement2.clone()]),
                ],
                else_block: vec![statement3.clone()],
            }];
            let mut symbol_table = HashMap::new();
            symbol_table.insert(make_identifier!("abc"), Symbol::DeprecatedKey(0x0100));
            symbol_table.insert(make_identifier!("xyz"), Symbol::DeprecatedKey(0x0200));

            compile_to_raw(statements, symbol_table)
        }

        #[test]
        fn if_clause_satisfied() {
            // Binds when the if clause is satisfied.
            let raw_instructions = if_else_instructions();
            let properties = vec![
                DeviceProperty { key: 0x0100, value: 1 },
                DeviceProperty { key: 0x0200, value: 1 },
            ];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: true,
                        line: 1
                    },
                    DebuggerOutput::ConditionStatement {
                        key: 0x0200,
                        condition: RawCondition::Equal,
                        success: true,
                        line: 2
                    }
                ]
            );
        }

        #[test]
        fn if_else_clause_satisfied() {
            // Binds when the if else clause is satisfied.
            let raw_instructions = if_else_instructions();
            let properties = vec![
                DeviceProperty { key: 0x0100, value: 2 },
                DeviceProperty { key: 0x0200, value: 2 },
            ];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 1
                    },
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: true,
                        line: 3
                    },
                    DebuggerOutput::ConditionStatement {
                        key: 0x0200,
                        condition: RawCondition::Equal,
                        success: true,
                        line: 4
                    }
                ]
            );
        }

        #[test]
        fn else_clause_satisfied() {
            // Binds when the else clause is satisfied.
            let raw_instructions = if_else_instructions();
            let properties = vec![DeviceProperty { key: 0x0200, value: 3 }];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 1
                    },
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 3
                    },
                    DebuggerOutput::ConditionStatement {
                        key: 0x0200,
                        condition: RawCondition::Equal,
                        success: true,
                        line: 6
                    }
                ]
            );
        }

        #[test]
        fn incorrect_values() {
            // Doesn't bind when the device has incorrect values for the properties.
            let raw_instructions = if_else_instructions();
            let properties = vec![
                DeviceProperty { key: 0x0100, value: 42 },
                DeviceProperty { key: 0x0200, value: 42 },
            ];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 1
                    },
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 3
                    },
                    DebuggerOutput::ConditionStatement {
                        key: 0x0200,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 6
                    }
                ]
            );
        }

        #[test]
        fn missing_values() {
            // Doesn't bind when the properties are missing in the device.
            let raw_instructions = if_else_instructions();
            let properties = Vec::new();
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 1
                    },
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 3
                    },
                    DebuggerOutput::ConditionStatement {
                        key: 0x0200,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 6
                    }
                ]
            );
        }
    }

    mod abort {
        use super::*;

        fn abort_instructions() -> Vec<RawInstruction<[u32; 3]>> {
            let statements = vec![
                Statement::ConditionStatement {
                    span: span_with_line(7),
                    condition: Condition {
                        span: span_with_line(7),
                        lhs: make_identifier!("abc"),
                        op: ConditionOp::Equals,
                        rhs: Value::NumericLiteral(42),
                    },
                },
                Statement::Abort { span: span_with_line(8) },
            ];
            let mut symbol_table = HashMap::new();
            symbol_table.insert(make_identifier!("abc"), Symbol::DeprecatedKey(0x0100));

            compile_to_raw(statements, symbol_table)
        }

        #[test]
        fn aborts() {
            // Doesn't bind when abort statement is present.
            let raw_instructions = abort_instructions();
            let properties = vec![
                DeviceProperty { key: 0x0100, value: 42 },
                DeviceProperty { key: 0x0200, value: 1 },
            ];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::ConditionStatement {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: true,
                        line: 7
                    },
                    DebuggerOutput::AbortStatement { line: 8 },
                ]
            );
        }
    }

    mod full_program {
        use super::*;

        fn full_program_instructions() -> Vec<RawInstruction<[u32; 3]>> {
            /*
            if abc == 42 {
                abort;
            } else {
                accept xyz {1, 2};
                pqr != 5;
            }
            */

            let condition = Condition {
                span: span_with_line(1),
                lhs: make_identifier!("abc"),
                op: ConditionOp::Equals,
                rhs: Value::NumericLiteral(42),
            };
            let abort_statement = Statement::Abort { span: span_with_line(2) };
            let accept_statement = Statement::Accept {
                span: span_with_line(4),
                identifier: make_identifier!("xyz"),
                values: vec![Value::NumericLiteral(1), Value::NumericLiteral(2)],
            };
            let condition_statement = Statement::ConditionStatement {
                span: span_with_line(5),
                condition: Condition {
                    span: span_with_line(5),
                    lhs: make_identifier!("pqr"),
                    op: ConditionOp::NotEquals,
                    rhs: Value::NumericLiteral(5),
                },
            };

            let statements = vec![Statement::If {
                span: span_with_line(1),
                blocks: vec![(condition, vec![abort_statement])],
                else_block: vec![accept_statement, condition_statement],
            }];
            let mut symbol_table = HashMap::new();
            symbol_table.insert(make_identifier!("abc"), Symbol::DeprecatedKey(0x0100));
            symbol_table.insert(make_identifier!("xyz"), Symbol::DeprecatedKey(0x0200));
            symbol_table.insert(make_identifier!("pqr"), Symbol::DeprecatedKey(0x0300));

            compile_to_raw(statements, symbol_table)
        }

        #[test]
        fn if_condition_satisfied() {
            // Aborts because if condition is true.
            let raw_instructions = full_program_instructions();
            let properties = vec![DeviceProperty { key: 0x0100, value: 42 }];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: true,
                        line: 1
                    },
                    DebuggerOutput::AbortStatement { line: 2 },
                ]
            );
        }

        #[test]
        fn else_block_satisfied() {
            // Binds because all statements inside else block are satisfied.
            let raw_instructions = full_program_instructions();
            let properties = vec![
                DeviceProperty { key: 0x0100, value: 43 },
                DeviceProperty { key: 0x0200, value: 1 },
            ];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 1
                    },
                    DebuggerOutput::AcceptStatementSuccess { key: 0x0200, value: 1, line: 4 },
                    DebuggerOutput::ConditionStatement {
                        key: 0x0300,
                        condition: RawCondition::NotEqual,
                        success: true,
                        line: 5
                    },
                ]
            );
        }

        #[test]
        fn accept_statement_not_satisfied() {
            // Doesn't bind because accept statement is not satisfied.
            let raw_instructions = full_program_instructions();
            let properties = vec![
                DeviceProperty { key: 0x0100, value: 43 },
                DeviceProperty { key: 0x0200, value: 3 },
            ];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 1
                    },
                    DebuggerOutput::AcceptStatementFailure { key: 0x0200, line: 4 },
                ]
            );
        }

        #[test]
        fn condition_statement_not_satisfied() {
            // Doesn't bind because condition statement is not satisfied.
            let raw_instructions = full_program_instructions();
            let properties = vec![
                DeviceProperty { key: 0x0100, value: 43 },
                DeviceProperty { key: 0x0200, value: 1 },
                DeviceProperty { key: 0x0300, value: 5 },
            ];
            let mut debugger = Debugger::new(&raw_instructions, &properties).unwrap();
            assert!(!debugger.evaluate_bind_program().unwrap());
            assert_eq!(
                debugger.output,
                vec![
                    DebuggerOutput::IfCondition {
                        key: 0x0100,
                        condition: RawCondition::Equal,
                        success: false,
                        line: 1
                    },
                    DebuggerOutput::AcceptStatementSuccess { key: 0x0200, value: 1, line: 4 },
                    DebuggerOutput::ConditionStatement {
                        key: 0x0300,
                        condition: RawCondition::NotEqual,
                        success: false,
                        line: 5
                    },
                ]
            );
        }
    }
}
