// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bind_library;
use crate::bind_program::{self, Condition, ConditionOp, Statement};
use crate::ddk_bind_constants::BIND_AUTOBIND;
use crate::dependency_graph::{self, DependencyGraph};
use crate::encode_bind_program_v1::{encode_to_bytecode_v1, encode_to_string_v1};
use crate::encode_bind_program_v2::{encode_to_bytecode_v2, encode_to_string_v2};
use crate::errors::UserError;
use crate::instruction;
use crate::linter;
use crate::make_identifier;
use crate::offline_debugger::AstLocation;
use crate::parser_common::{self, CompoundIdentifier, Include, Value};
use std::collections::HashMap;
use std::convert::TryFrom;
use std::fmt;
use std::ops::Deref;
use thiserror::Error;

#[derive(Debug, Error, Clone, PartialEq)]
pub enum CompilerError {
    BindParserError(parser_common::BindParserError),
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
}

impl fmt::Display for CompilerError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

#[derive(Debug, Error, Clone, PartialEq)]
pub enum BindProgramEncodeError {
    InvalidStringLength(String),
    DuplicateSymbol(String),
    UnsupportedSymbol,
    IntegerOutOfRange,
    MismatchValueTypes(Symbol, Symbol),
    MissingStringInSymbolTable(String),
}

impl fmt::Display for BindProgramEncodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

#[derive(Debug, PartialEq)]
pub struct BindProgram<'a> {
    pub symbol_table: SymbolTable,
    pub instructions: Vec<SymbolicInstructionInfo<'a>>,
}

pub fn encode_to_bytecode(
    bind_program: BindProgram,
    use_new_bytecode: bool,
) -> Result<Vec<u8>, BindProgramEncodeError> {
    if use_new_bytecode {
        return encode_to_bytecode_v2(bind_program);
    }

    encode_to_bytecode_v1(bind_program)
}

pub fn encode_to_string(
    bind_program: BindProgram,
    use_new_bytecode: bool,
) -> Result<String, BindProgramEncodeError> {
    if use_new_bytecode {
        return encode_to_string_v2(bind_program);
    }

    encode_to_string_v1(bind_program)
}

pub type SymbolTable = HashMap<CompoundIdentifier, Symbol>;

pub fn compile<'a>(
    program_str: &'a str,
    libraries: &[String],
    lint: bool,
) -> Result<BindProgram<'a>, CompilerError> {
    let ast = bind_program::Ast::try_from(program_str).map_err(CompilerError::BindParserError)?;

    let library_asts: Vec<bind_library::Ast> = libraries
        .into_iter()
        .map(|lib| {
            let ast = bind_library::Ast::try_from(lib.as_str())
                .map_err(CompilerError::BindParserError)?;
            if lint {
                linter::lint_library(&ast).map_err(CompilerError::LinterError)?;
            }
            Ok(ast)
        })
        .collect::<Result<_, CompilerError>>()?;

    let dependencies = resolve_dependencies(&ast, library_asts.iter())?;
    let symbol_table = construct_symbol_table(dependencies.into_iter())?;
    compile_statements(ast.statements, symbol_table)
}

fn resolve_dependencies<'a>(
    program: &bind_program::Ast,
    libraries: impl Iterator<Item = &'a bind_library::Ast> + Clone,
) -> Result<Vec<&'a bind_library::Ast>, CompilerError> {
    (|| {
        let mut graph = DependencyGraph::new();

        for library in libraries.clone() {
            graph.insert_node(library.name.clone(), library);
        }

        for Include { name, .. } in &program.using {
            graph.insert_edge_from_root(name)?;
        }

        for from in libraries {
            for to in &from.using {
                graph.insert_edge(&from.name, &to.name)?;
            }
        }

        graph.resolve()
    })()
    .map_err(CompilerError::DependencyError)
}

#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum Symbol {
    DeprecatedKey(u32),
    Key(String, bind_library::ValueType),
    NumberValue(u64),
    StringValue(String),
    BoolValue(bool),
    EnumValue,
}

/// Find the namespace of a qualified identifier from the library's includes. Or, if the identifier
/// is unqualified, return the local qualified identifier.
fn find_qualified_identifier(
    declaration: &bind_library::Declaration,
    using: &Vec<parser_common::Include>,
    local_qualified: &CompoundIdentifier,
) -> Result<CompoundIdentifier, CompilerError> {
    if let Some(namespace) = declaration.identifier.parent() {
        // A declaration of a qualified (i.e. non-local) key must be an extension.
        if !declaration.extends {
            return Err(CompilerError::MissingExtendsKeyword(declaration.identifier.clone()));
        }

        // Special case for deprecated symbols (currently in the fuchsia namespace), return the
        // declaration as-is.
        if namespace == make_identifier!["fuchsia"] {
            return Ok(declaration.identifier.clone());
        }

        // Find the fully qualified name from the included libraries.
        let include = using
            .iter()
            .find(|include| {
                namespace == include.name || Some(namespace.to_string()) == include.alias
            })
            .ok_or(CompilerError::UnresolvedQualification(declaration.identifier.clone()))?;

        return Ok(include.name.nest(declaration.identifier.name.clone()));
    }

    // It is not valid to extend an unqualified (i.e. local) key.
    if declaration.extends {
        return Err(CompilerError::InvalidExtendsKeyword(local_qualified.clone()));
    }

    // An unqualified/local key is scoped to the current library.
    Ok(local_qualified.clone())
}

/// Construct a map of every key and value defined by `libraries`. The identifiers in the symbol
/// table will be fully qualified, i.e. they will contain their full namespace. A symbol is
/// namespaced according to the name of the library it is defined in. If a library defines a value
/// by extending a previously defined key, then that value will be namespaced to the current library
/// and not the library of its key.
#[allow(dead_code)]
fn construct_symbol_table(
    libraries: impl Iterator<Item = impl Deref<Target = bind_library::Ast>>,
) -> Result<SymbolTable, CompilerError> {
    let mut symbol_table = get_deprecated_symbols();
    for lib in libraries {
        let bind_library::Ast { name, using, declarations } = &*lib;

        for declaration in declarations {
            // Construct a qualified identifier for this key that's namespaced to the current
            // library, discarding any other qualifiers. This identifier is used to scope values
            // defined under this key.
            let local_qualified = name.nest(declaration.identifier.name.clone());

            // Attempt to match the namespace of the key to an include of the current library, or if
            // it is unqualified use the local qualified name. Also do a first pass at checking
            // whether the extend keyword is used correctly.
            let qualified = find_qualified_identifier(declaration, using, &local_qualified)?;

            // Type-check the qualified name against the existing symbols, and check that extended
            // keys are previously defined and that non-extended keys are not.
            match symbol_table.get(&qualified) {
                Some(Symbol::Key(_, value_type)) => {
                    if !declaration.extends {
                        return Err(CompilerError::DuplicateIdentifier(qualified));
                    }
                    if declaration.value_type != *value_type {
                        return Err(CompilerError::TypeMismatch(qualified));
                    }
                }
                Some(Symbol::DeprecatedKey(_)) => (),
                Some(_) => {
                    return Err(CompilerError::TypeMismatch(qualified));
                }
                None => {
                    if declaration.extends {
                        return Err(CompilerError::UndeclaredKey(qualified));
                    }
                    symbol_table.insert(
                        qualified.clone(),
                        Symbol::Key(qualified.to_string(), declaration.value_type),
                    );
                }
            }

            // Insert each value associated with the declaration into the symbol table, taking care
            // to scope each identifier under the locally qualified identifier of the key. We don't
            // need to type-check values here since the parser has already done that.
            for value in &declaration.values {
                let qualified_value = local_qualified.nest(value.identifier().to_string());
                if symbol_table.contains_key(&qualified_value) {
                    return Err(CompilerError::DuplicateIdentifier(qualified_value));
                }

                match value {
                    bind_library::Value::Number(_, value) => {
                        symbol_table.insert(qualified_value, Symbol::NumberValue(*value));
                    }
                    bind_library::Value::Str(_, value) => {
                        symbol_table.insert(qualified_value, Symbol::StringValue(value.clone()));
                    }
                    bind_library::Value::Bool(_, value) => {
                        symbol_table.insert(qualified_value, Symbol::BoolValue(*value));
                    }
                    bind_library::Value::Enum(_) => {
                        symbol_table.insert(qualified_value, Symbol::EnumValue);
                    }
                };
            }
        }
    }

    Ok(symbol_table)
}

/// Hard code these symbols during the migration from macros to bind programs. Eventually these
/// will be defined in libraries and the compiler will emit strings for them in the bytecode.
fn deprecated_keys() -> Vec<(String, u32)> {
    let mut keys = Vec::new();

    keys.push(("BIND_PROTOCOL".to_string(), 0x0001));

    keys.push(("BIND_AUTOBIND".to_string(), 0x0002));

    keys.push(("BIND_PLATFORM_DEV_VID".to_string(), 0x0300));
    keys.push(("BIND_PCI_VID".to_string(), 0x0100));

    keys.push(("BIND_PCI_DID".to_string(), 0x0101));
    keys.push(("BIND_PCI_CLASS".to_string(), 0x0102));
    keys.push(("BIND_PCI_SUBCLASS".to_string(), 0x0103));
    keys.push(("BIND_PCI_INTERFACE".to_string(), 0x0104));
    keys.push(("BIND_PCI_REVISION".to_string(), 0x0105));

    // usb binding variables at 0x02XX
    // these are used for both ZX_PROTOCOL_USB_INTERFACE and ZX_PROTOCOL_USB_FUNCTION
    keys.push(("BIND_USB_VID".to_string(), 0x0200));
    keys.push(("BIND_USB_PID".to_string(), 0x0201));
    keys.push(("BIND_USB_CLASS".to_string(), 0x0202));
    keys.push(("BIND_USB_SUBCLASS".to_string(), 0x0203));
    keys.push(("BIND_USB_PROTOCOL".to_string(), 0x0204));

    // Platform bus binding variables at 0x03XX
    keys.push(("BIND_PLATFORM_DEV_VID".to_string(), 0x0300));
    keys.push(("BIND_PLATFORM_DEV_PID".to_string(), 0x0301));
    keys.push(("BIND_PLATFORM_DEV_DID".to_string(), 0x0302));
    keys.push(("BIND_PLATFORM_PROTO".to_string(), 0x0303));
    keys.push(("BIND_PLATFORM_DEV_INSTANCE_ID".to_string(), 0x0304));

    // ACPI binding variables at 0x04XX
    // The _HID is a 7- or 8-byte string. Because a bind property is 32-bit, use 2
    // properties to bind using the _HID. They are encoded in big endian order for
    // human readability. In the case of 7-byte _HID's, the 8th-byte shall be 0.
    keys.push(("BIND_ACPI_HID_0_3".to_string(), 0x0400));
    keys.push(("BIND_ACPI_HID_4_7".to_string(), 0x0401));
    // The _CID may be a valid HID value or a bus-specific string. The ACPI bus
    // driver only publishes those that are valid HID values.
    keys.push(("BIND_ACPI_CID_0_3".to_string(), 0x0402));
    keys.push(("BIND_ACPI_CID_4_7".to_string(), 0x0403));

    // Intel HDA Codec binding variables at 0x05XX
    keys.push(("BIND_IHDA_CODEC_VID".to_string(), 0x0500));
    keys.push(("BIND_IHDA_CODEC_DID".to_string(), 0x0501));
    keys.push(("BIND_IHDA_CODEC_MAJOR_REV".to_string(), 0x0502));
    keys.push(("BIND_IHDA_CODEC_MINOR_REV".to_string(), 0x0503));
    keys.push(("BIND_IHDA_CODEC_VENDOR_REV".to_string(), 0x0504));
    keys.push(("BIND_IHDA_CODEC_VENDOR_STEP".to_string(), 0x0505));

    // Serial binding variables at 0x06XX
    keys.push(("BIND_SERIAL_CLASS".to_string(), 0x0600));
    keys.push(("BIND_SERIAL_VID".to_string(), 0x0601));
    keys.push(("BIND_SERIAL_PID".to_string(), 0x0602));

    // NAND binding variables at 0x07XX
    keys.push(("BIND_NAND_CLASS".to_string(), 0x0700));

    // Bluetooth binding variables at 0x08XX
    keys.push(("BIND_BT_GATT_SVC_UUID16".to_string(), 0x0800));
    // 128-bit UUID is split across 4 32-bit unsigned ints
    keys.push(("BIND_BT_GATT_SVC_UUID128_1".to_string(), 0x0801));
    keys.push(("BIND_BT_GATT_SVC_UUID128_2".to_string(), 0x0802));
    keys.push(("BIND_BT_GATT_SVC_UUID128_3".to_string(), 0x0803));
    keys.push(("BIND_BT_GATT_SVC_UUID128_4".to_string(), 0x0804));

    // SDIO binding variables at 0x09XX
    keys.push(("BIND_SDIO_VID".to_string(), 0x0900));
    keys.push(("BIND_SDIO_PID".to_string(), 0x0901));
    keys.push(("BIND_SDIO_FUNCTION".to_string(), 0x0902));

    // I2C binding variables at 0x0A0X
    keys.push(("BIND_I2C_CLASS".to_string(), 0x0A00));
    keys.push(("BIND_I2C_BUS_ID".to_string(), 0x0A01));
    keys.push(("BIND_I2C_ADDRESS".to_string(), 0x0A02));

    // GPIO binding variables at 0x0A1X
    keys.push(("BIND_GPIO_PIN".to_string(), 0x0A10));

    // POWER binding variables at 0x0A2X
    keys.push(("BIND_POWER_DOMAIN".to_string(), 0x0A20));
    keys.push(("BIND_POWER_DOMAIN_COMPOSITE".to_string(), 0x0A21));

    // POWER binding variables at 0x0A3X
    keys.push(("BIND_CLOCK_ID".to_string(), 0x0A30));

    // SPI binding variables at 0x0A4X
    keys.push(("BIND_SPI_CLASS".to_string(), 0x0A40));
    keys.push(("BIND_SPI_BUS_ID".to_string(), 0x0A41));
    keys.push(("BIND_SPI_CHIP_SELECT".to_string(), 0x0A42));

    // Registers binding variables at 0x0A8X
    keys.push(("BIND_REGISTER_ID".to_string(), 0x0A80));

    // Fuchsia-defined topological path properties are at 0x0B00 through 0x0B7F.
    // Vendor-defined topological path properties are at 0x0B80 to 0x0BFF.
    // For vendor properties, it is recommended that a vendor ID be included
    // and checked via some other property.
    keys.push(("BIND_TOPO_START".to_string(), 0x0B00));
    keys.push(("BIND_TOPO_PCI".to_string(), 0x0B00));
    keys.push(("BIND_TOPO_I2C".to_string(), 0x0B01));
    keys.push(("BIND_TOPO_SPI".to_string(), 0x0B02));
    keys.push(("BIND_TOPO_VENDOR_START".to_string(), 0x0B80));
    keys.push(("BIND_TOPO_VENDOR_END".to_string(), 0x0BFF));
    keys.push(("BIND_TOPO_END".to_string(), 0x0BFF));

    keys
}

fn get_deprecated_symbols() -> SymbolTable {
    let mut symbol_table = HashMap::new();
    for (key, value) in deprecated_keys() {
        symbol_table.insert(make_identifier!("fuchsia", key), Symbol::DeprecatedKey(value));
    }
    symbol_table
}

pub fn get_deprecated_key_identifiers() -> HashMap<u32, String> {
    let mut key_identifiers = HashMap::new();
    for (key, value) in deprecated_keys() {
        key_identifiers.insert(value, make_identifier!("fuchsia", key).to_string());
    }
    key_identifiers
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

    pub fn create_autobind() -> Self {
        SymbolicInstructionInfo {
            location: None,
            instruction: SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::NumberValue(BIND_AUTOBIND.into()),
                rhs: Symbol::NumberValue(0),
            },
        }
    }
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

pub fn compile_statements<'a>(
    statements: Vec<Statement<'a>>,
    symbol_table: SymbolTable,
) -> Result<BindProgram<'a>, CompilerError> {
    let mut compiler = Compiler::new(symbol_table);
    compiler.compile_statements(statements)?;
    Ok(compiler.bind_program)
}

struct Compiler<'a> {
    bind_program: BindProgram<'a>,
    next_label_id: u32,
}

impl<'a> Compiler<'a> {
    fn new(symbol_table: SymbolTable) -> Self {
        Compiler {
            bind_program: BindProgram { instructions: vec![], symbol_table: symbol_table },
            next_label_id: 0,
        }
    }

    fn lookup_identifier(&self, identifier: &CompoundIdentifier) -> Result<Symbol, CompilerError> {
        let symbol = self
            .bind_program
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
                .bind_program
                .symbol_table
                .get(ident)
                .ok_or(CompilerError::UnknownKey(ident.clone()))
                .map(|x| x.clone()),
        }
    }

    fn compile_statements(&mut self, statements: Vec<Statement<'a>>) -> Result<(), CompilerError> {
        self.compile_block(statements)?;

        // If none of the statements caused an abort, then we should bind the driver.
        self.bind_program.instructions.push(SymbolicInstructionInfo {
            location: None,
            instruction: SymbolicInstruction::UnconditionalBind,
        });

        Ok(())
    }

    fn get_unique_label(&mut self) -> u32 {
        let label = self.next_label_id;
        self.next_label_id += 1;
        label
    }

    fn compile_block(&mut self, statements: Vec<Statement<'a>>) -> Result<(), CompilerError> {
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
                        self.bind_program.instructions.push(SymbolicInstructionInfo {
                            location: Some(AstLocation::ConditionStatement(statement)),
                            instruction,
                        });
                    }
                }
                Statement::Accept { span, identifier, values } => {
                    let lhs_symbol = self.lookup_identifier(&identifier)?;
                    let label_id = self.get_unique_label();
                    for value in values {
                        self.bind_program.instructions.push(SymbolicInstructionInfo {
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
                    self.bind_program.instructions.push(SymbolicInstructionInfo {
                        location: Some(AstLocation::AcceptStatementFailure {
                            identifier,
                            symbol: lhs_symbol,
                            span,
                        }),
                        instruction: SymbolicInstruction::UnconditionalAbort,
                    });
                    self.bind_program.instructions.push(SymbolicInstructionInfo {
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
                        self.bind_program.instructions.push(SymbolicInstructionInfo {
                            location: Some(AstLocation::IfCondition(condition)),
                            instruction,
                        });

                        // Compile the block itself.
                        self.compile_block(block_statements)?;

                        // Jump to after the if statement.
                        self.bind_program.instructions.push(SymbolicInstructionInfo {
                            location: None,
                            instruction: SymbolicInstruction::UnconditionalJump {
                                label: final_label_id,
                            },
                        });

                        // Insert a label to jump to when the condition fails.
                        self.bind_program.instructions.push(SymbolicInstructionInfo {
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

                    self.bind_program.instructions.push(SymbolicInstructionInfo {
                        location: None,
                        instruction: SymbolicInstruction::Label(final_label_id),
                    });
                }
                Statement::Abort { span: _ } => {
                    self.bind_program.instructions.push(SymbolicInstructionInfo {
                        location: Some(AstLocation::AbortStatement(statement)),
                        instruction: SymbolicInstruction::UnconditionalAbort,
                    });
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::parser_common::Span;

    mod symbol_table {
        use super::*;

        #[test]
        fn simple_key_and_value() {
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("test"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["symbol"],
                    value_type: bind_library::ValueType::Number,
                    extends: false,
                    values: vec![(bind_library::Value::Number("x".to_string(), 1))],
                }],
            }];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("test", "symbol")),
                Some(&Symbol::Key("test.symbol".to_string(), bind_library::ValueType::Number))
            );
            assert_eq!(
                st.get(&make_identifier!("test", "symbol", "x")),
                Some(&Symbol::NumberValue(1))
            );
        }

        #[test]
        fn extension() {
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![(bind_library::Value::Number("x".to_string(), 1))],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![Include { name: make_identifier!("lib_a"), alias: None }],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["lib_a", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: true,
                        values: vec![(bind_library::Value::Number("y".to_string(), 2))],
                    }],
                },
            ];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol")),
                Some(&Symbol::Key("lib_a.symbol".to_string(), bind_library::ValueType::Number))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol", "x")),
                Some(&Symbol::NumberValue(1))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_b", "symbol", "y")),
                Some(&Symbol::NumberValue(2))
            );
        }

        #[test]
        fn aliased_extension() {
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![(bind_library::Value::Number("x".to_string(), 1))],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![Include {
                        name: make_identifier!("lib_a"),
                        alias: Some("alias".to_string()),
                    }],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["alias", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: true,
                        values: vec![(bind_library::Value::Number("y".to_string(), 2))],
                    }],
                },
            ];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol")),
                Some(&Symbol::Key("lib_a.symbol".to_string(), bind_library::ValueType::Number))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol", "x")),
                Some(&Symbol::NumberValue(1))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_b", "symbol", "y")),
                Some(&Symbol::NumberValue(2))
            );
        }

        #[test]
        fn deprecated_key_extension() {
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("lib_a"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["fuchsia", "BIND_PCI_DID"],
                    value_type: bind_library::ValueType::Number,
                    extends: true,
                    values: vec![(bind_library::Value::Number("x".to_string(), 0x1234))],
                }],
            }];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("lib_a", "BIND_PCI_DID", "x")),
                Some(&Symbol::NumberValue(0x1234))
            );
        }

        #[test]
        fn duplicate_key() {
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("test"),
                using: vec![],
                declarations: vec![
                    bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    },
                    bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    },
                ],
            }];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::DuplicateIdentifier(make_identifier!("test", "symbol")))
            );
        }

        #[test]
        fn duplicate_value() {
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("test"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["symbol"],
                    value_type: bind_library::ValueType::Number,
                    extends: false,
                    values: vec![
                        bind_library::Value::Number("a".to_string(), 1),
                        bind_library::Value::Number("a".to_string(), 2),
                    ],
                }],
            }];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::DuplicateIdentifier(make_identifier!("test", "symbol", "a")))
            );
        }

        #[test]
        fn keys_are_qualified() {
            // The same symbol declared in two libraries should not collide.
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    }],
                },
            ];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol")),
                Some(&Symbol::Key("lib_a.symbol".to_string(), bind_library::ValueType::Number))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_b", "symbol")),
                Some(&Symbol::Key("lib_b.symbol".to_string(), bind_library::ValueType::Number))
            );
        }

        #[test]
        fn missing_extend_keyword() {
            // A library referring to a previously declared symbol must use the "extend" keyword.
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["lib_a", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    }],
                },
            ];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::MissingExtendsKeyword(make_identifier!("lib_a", "symbol")))
            );
        }

        #[test]
        fn invalid_extend_keyword() {
            // A library cannot declare an unqualified (and therefore locally namespaced) symbol
            // with the "extend" keyword.
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("lib_a"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["symbol"],
                    value_type: bind_library::ValueType::Number,
                    extends: true,
                    values: vec![],
                }],
            }];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::InvalidExtendsKeyword(make_identifier!("lib_a", "symbol")))
            );
        }

        #[test]
        fn unresolved_qualification() {
            // A library cannot refer to a qualified identifier where the qualifier is not in its
            // list of includes.
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("lib_a"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["lib_b", "symbol"],
                    value_type: bind_library::ValueType::Number,
                    extends: true,
                    values: vec![],
                }],
            }];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::UnresolvedQualification(make_identifier!("lib_b", "symbol")))
            );
        }

        #[test]
        fn undeclared_key() {
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![Include { name: make_identifier!("lib_a"), alias: None }],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["lib_a", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: true,
                        values: vec![],
                    }],
                },
            ];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::UndeclaredKey(make_identifier!("lib_a", "symbol")))
            );
        }

        #[test]
        fn type_mismatch() {
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Str,
                        extends: false,
                        values: vec![],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![Include { name: make_identifier!("lib_a"), alias: None }],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["lib_a", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: true,
                        values: vec![],
                    }],
                },
            ];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::TypeMismatch(make_identifier!("lib_a", "symbol")))
            );
        }
    }

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

        let program =
            bind_program::Ast { using: vec![], statements: vec![condition_statement.clone()] };
        let mut symbol_table = HashMap::new();
        symbol_table.insert(
            make_identifier!("abc"),
            Symbol::Key("abc".to_string(), bind_library::ValueType::Number),
        );

        assert_eq!(
            compile_statements(program.statements, symbol_table).unwrap().instructions,
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
        let program = bind_program::Ast {
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
            compile_statements(program.statements, symbol_table).unwrap().instructions,
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

        let program = bind_program::Ast {
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
            compile_statements(program.statements, symbol_table).unwrap().instructions,
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
        let program = bind_program::Ast {
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
            compile_statements(program.statements, symbol_table),
            Err(CompilerError::IfStatementMustBeTerminal)
        );
    }

    #[test]
    fn abort() {
        let abort_statement = Statement::Abort { span: Span::new() };

        let program =
            bind_program::Ast { using: vec![], statements: vec![abort_statement.clone()] };
        let symbol_table = HashMap::new();

        assert_eq!(
            compile_statements(program.statements, symbol_table).unwrap().instructions,
            vec![
                SymbolicInstructionInfo {
                    location: Some(AstLocation::AbortStatement(abort_statement)),
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
    fn dependencies() {
        let program = bind_program::Ast {
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
            resolve_dependencies(&program, libraries.iter()),
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
        let program = bind_program::Ast {
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
            resolve_dependencies(&program, libraries.iter()),
            Err(CompilerError::DependencyError(
                dependency_graph::DependencyError::MissingDependency(make_identifier!("A", "B"))
            ))
        );
    }
}
