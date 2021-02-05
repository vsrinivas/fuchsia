// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler::{BindProgramEncodeError, CompilerError};
use crate::debugger;
use crate::dependency_graph::DependencyError;
use crate::linter::LinterError;
use crate::offline_debugger;
use crate::parser_common::{BindParserError, CompoundIdentifier};
use crate::test;
use std::fmt;

pub struct UserError {
    index: String,
    message: String,
    span: Option<String>,
    is_compiler_bug: bool,
}

impl UserError {
    fn new(index: &str, message: &str, span: Option<String>, is_compiler_bug: bool) -> Self {
        UserError { index: index.to_string(), message: message.to_string(), span, is_compiler_bug }
    }
}

impl fmt::Display for UserError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "[{}]: {}", self.index, self.message)?;
        if self.is_compiler_bug {
            writeln!(f, "This is a bind compiler bug, please report it!")?;
        }
        if let Some(span) = &self.span {
            writeln!(f, "{}", span)?;
        }
        Ok(())
    }
}

impl From<BindParserError> for UserError {
    fn from(error: BindParserError) -> Self {
        match error {
            BindParserError::Type(span) => {
                UserError::new("E001", "Expected a type keyword.", Some(span), false)
            }
            BindParserError::StringLiteral(span) => {
                UserError::new("E002", "Expected a string literal.", Some(span), false)
            }
            BindParserError::NumericLiteral(span) => {
                UserError::new("E003", "Expected a numerical literal.", Some(span), false)
            }
            BindParserError::BoolLiteral(span) => {
                UserError::new("E004", "Expected a boolean literal.", Some(span), false)
            }
            BindParserError::Identifier(span) => {
                UserError::new("E005", "Expected an identifier.", Some(span), false)
            }
            BindParserError::Semicolon(span) => {
                UserError::new("E006", "Missing semicolon (;).", Some(span), false)
            }
            BindParserError::Assignment(span) => UserError::new(
                "E007",
                "Expected an assignment. Are you missing a '='?",
                Some(span),
                false,
            ),
            BindParserError::ListStart(span) => {
                UserError::new("E008", "Expected a list. Are you missing a '{'?", Some(span), false)
            }
            BindParserError::ListEnd(span) => UserError::new(
                "E009",
                "List does not terminate. Are you missing a '}'?",
                Some(span),
                false,
            ),
            BindParserError::ListSeparator(span) => UserError::new(
                "E010",
                "Expected a list separator. Are you missing a ','?",
                Some(span),
                false,
            ),
            BindParserError::LibraryKeyword(span) => {
                UserError::new("E011", "Expected 'library' keyword.", Some(span), false)
            }
            BindParserError::UsingKeyword(span) => {
                UserError::new("E012", "Expected 'using' keyword.", Some(span), false)
            }
            BindParserError::AsKeyword(span) => {
                UserError::new("E013", "Expected 'as' keyword", Some(span), false)
            }
            BindParserError::IfBlockStart(span) => UserError::new(
                "E014",
                "Expected '{' to begin if statement block.",
                Some(span),
                false,
            ),
            BindParserError::IfBlockEnd(span) => {
                UserError::new("E015", "Expected '}' to end if statement block.", Some(span), false)
            }
            BindParserError::IfKeyword(span) => {
                UserError::new("E016", "Expected 'if' keyword.", Some(span), false)
            }
            BindParserError::ElseKeyword(span) => {
                UserError::new("E017", "Expected 'else' keyword", Some(span), false)
            }
            BindParserError::ConditionOp(span) => UserError::new(
                "E018",
                "Expected a condition operation ('==' or '!='.)",
                Some(span),
                false,
            ),
            BindParserError::ConditionValue(span) => UserError::new(
                "E019",
                "Expected a condition value: string, number, boolean, or identifier",
                Some(span),
                false,
            ),
            BindParserError::AcceptKeyword(span) => {
                UserError::new("E020", "Expected 'accept' keyword.", Some(span), false)
            }
            BindParserError::AbortKeyword(span) => {
                UserError::new("E024", "Expected 'abort' keyword.", Some(span), false)
            }
            BindParserError::NoStatements(span) => UserError::new(
                "E021",
                "Bind programs must contain at least one statement.",
                Some(span),
                false,
            ),
            BindParserError::Eof(span) => {
                UserError::new("E025", "Expected end of file.", Some(span), false)
            }
            BindParserError::UnterminatedComment => {
                UserError::new("E023", "Found an unterminated multiline comment.", None, false)
            }
            BindParserError::Unknown(span, kind) => UserError::new(
                "E022",
                &format!("Unexpected parser error: {:?}.", kind),
                Some(span),
                true,
            ),
        }
    }
}

impl From<CompilerError> for UserError {
    fn from(error: CompilerError) -> Self {
        match error {
            CompilerError::BindParserError(error) => UserError::from(error),
            CompilerError::DependencyError(error) => UserError::from(error),
            CompilerError::LinterError(error) => UserError::from(error),
            CompilerError::DuplicateIdentifier(identifier) => UserError::new(
                "E102",
                &format!("The identifier `{}` is defined multiple times.", identifier),
                None,
                false,
            ),
            CompilerError::TypeMismatch(identifier) => UserError::new(
                "E103",
                &format!("The identifier `{}` is extended with a mismatched type.", identifier),
                None,
                false,
            ),
            CompilerError::UnresolvedQualification(identifier) => UserError::new(
                "E104",
                &format!("Could not resolve the qualifier on `{}`.", identifier),
                None,
                false,
            ),
            CompilerError::UndeclaredKey(identifier) => UserError::new(
                "E105",
                &format!("Could not find previous definition of extended key `{}`.", identifier),
                None,
                false,
            ),
            CompilerError::MissingExtendsKeyword(identifier) => UserError::new(
                "E106",
                &format!(
                    "Cannot define a key with a namespace: `{}`. Are you missing an `extend`?",
                    identifier
                ),
                None,
                false,
            ),
            CompilerError::InvalidExtendsKeyword(identifier) => UserError::new(
                "E107",
                &format!("Cannot extend a key with no namespace: `{}`.", identifier),
                None,
                false,
            ),
            CompilerError::UnknownKey(identifier) => UserError::new(
                "E108",
                &format!("Bind program refers to undefined identifier: `{}`.", identifier),
                None,
                false,
            ),
            CompilerError::IfStatementMustBeTerminal => UserError::new(
                "E109",
                "If statements must be the last statement in a block",
                None,
                false,
            ),
        }
    }
}

impl From<DependencyError<CompoundIdentifier>> for UserError {
    fn from(error: DependencyError<CompoundIdentifier>) -> Self {
        match error {
            DependencyError::MissingDependency(library) => {
                UserError::new("E200", &format!("Missing dependency: {}", library), None, false)
            }
            DependencyError::CircularDependency => {
                UserError::new("E201", "Cicular dependency", None, false)
            }
        }
    }
}

impl From<offline_debugger::DebuggerError> for UserError {
    fn from(error: offline_debugger::DebuggerError) -> Self {
        match error {
            offline_debugger::DebuggerError::BindParserError(error) => UserError::from(error),
            offline_debugger::DebuggerError::CompilerError(error) => UserError::from(error),
            offline_debugger::DebuggerError::DuplicateKey(identifier) => UserError::new(
                "E300",
                &format!(
                    "The key `{}` appears multiple times in the device specification.",
                    identifier
                ),
                None,
                false,
            ),
            offline_debugger::DebuggerError::MissingLabel => UserError::new(
                "E301",
                "Missing label in the bind program symbolic instructions.",
                None,
                true,
            ),
            offline_debugger::DebuggerError::NoOutcome => UserError::new(
                "E302",
                "Reached the end of the symbolic instructions without binding or aborting.",
                None,
                true,
            ),
            offline_debugger::DebuggerError::IncorrectAstLocation => UserError::new(
                "E303",
                "Symbolic instruction has an incorrect AST location.",
                None,
                true,
            ),
            offline_debugger::DebuggerError::InvalidAstLocation => UserError::new(
                "E304",
                "AST location contains an incorrect statement type.",
                None,
                true,
            ),
            offline_debugger::DebuggerError::UnknownKey(identifier) => UserError::new(
                "E305",
                &format!("Statement contains an undefined identifier: `{}`.", identifier),
                None,
                true,
            ),
            offline_debugger::DebuggerError::InvalidValueSymbol(symbol) => UserError::new(
                "E306",
                &format!("Symbol is not a valid value type: `{:?}`.", symbol),
                None,
                true,
            ),
        }
    }
}

impl From<debugger::DebuggerError> for UserError {
    fn from(error: debugger::DebuggerError) -> Self {
        match error {
            debugger::DebuggerError::BindFlagsNotSupported => {
                UserError::new("E001", "The BIND_FLAGS property is not supported.", None, false)
            }
            debugger::DebuggerError::InvalidCondition(condition) => UserError::new(
                "E002",
                &format!(
                    "A bind program instruction contained an invalid condition: {}.",
                    condition
                ),
                None,
                true,
            ),
            debugger::DebuggerError::InvalidOperation(operation) => UserError::new(
                "E003",
                &format!(
                    "A bind program instruction contained an invalid operation: {}.",
                    operation
                ),
                None,
                true,
            ),
            debugger::DebuggerError::InvalidAstLocation(ast_location) => UserError::new(
                "E004",
                &format!(
                    "A bind program instruction contained an invalid AST location: {}.",
                    ast_location
                ),
                None,
                true,
            ),
            debugger::DebuggerError::IncorrectAstLocation => UserError::new(
                "E005",
                "The debugger only works with bind programs written in the new bind language.",
                None,
                false,
            ),
            debugger::DebuggerError::MissingLabel => UserError::new(
                "E006",
                "The bind program contained a GOTO with no matching LABEL.",
                None,
                true,
            ),
            debugger::DebuggerError::MissingBindProtocol => UserError::new(
                "E007",
                concat!(
                    "Device doesn't have a BIND_PROTOCOL property. ",
                    "The outcome of the bind program would depend on the device's protocol_id."
                ),
                None,
                false,
            ),
            debugger::DebuggerError::NoOutcome => UserError::new(
                "E008",
                "Reached the end of the instructions without binding or aborting.",
                None,
                true,
            ),
            debugger::DebuggerError::DuplicateKey(key) => UserError::new(
                "E009",
                &format!("The device has multiple values for the key {:#06x}.", key),
                None,
                false,
            ),
            debugger::DebuggerError::IncorrectCondition => {
                UserError::new("E010", "An incorrect condition type was encountered.", None, true)
            }
            debugger::DebuggerError::InvalidDeprecatedKey(key) => UserError::new(
                "E011",
                &format!("The bind program contained an invalid deprecated key: {:#06x}.", key),
                None,
                true,
            ),
        }
    }
}

impl From<BindProgramEncodeError> for UserError {
    fn from(error: BindProgramEncodeError) -> Self {
        match error {
            BindProgramEncodeError::InvalidStringLength(str) => UserError::new(
                "E600",
                &format!(
                    "The bind program contains a string that exceeds 255 characters: {}.",
                    str
                ),
                None,
                true,
            ),
            BindProgramEncodeError::DuplicateSymbol(str) => UserError::new(
                "E601",
                &format!("The bind program contains a duplicate symbol: {}.", str),
                None,
                true,
            ),
            BindProgramEncodeError::UnsupportedSymbol => UserError::new(
                "E602",
                "Symbol is not supported in the old bytecode format.",
                None,
                true,
            ),
            BindProgramEncodeError::IntegerOutOfRange => {
                UserError::new("E603", &format!("Integer out of range"), None, true)
            }
            BindProgramEncodeError::MismatchValueTypes(lhs, rhs) => UserError::new(
                "E604",
                &format!("Cannot compare different value types for {:?} and {:?}", lhs, rhs),
                None,
                true,
            ),
            BindProgramEncodeError::MissingStringInSymbolTable(str) => UserError::new(
                "E605",
                &format!("Missing string {} in symbol table", str),
                None,
                true,
            ),
            BindProgramEncodeError::DuplicateLabel(label_id) => UserError::new(
                "E606",
                &format!("Duplicate label {} in instructions", label_id),
                None,
                true,
            ),
            BindProgramEncodeError::MissingLabel(label_id) => UserError::new(
                "E607",
                &format!("Missing label {} in instructions", label_id),
                None,
                true,
            ),
            BindProgramEncodeError::InvalidGotoLocation(label_id) => UserError::new(
                "E608",
                &format!(
                    "Bind program cannot move backwards. Label {} appears before Goto statement",
                    label_id
                ),
                None,
                true,
            ),
            BindProgramEncodeError::JumpOffsetOutOfRange(label_id) => UserError::new(
                "E609",
                &format!("The jump offset for label {} exceeds 32 bits", label_id),
                None,
                true,
            ),
            BindProgramEncodeError::MatchNotSupported => UserError::new(
                "E610",
                "Match instructions are not supported in the new bytecode",
                None,
                true,
            ),
        }
    }
}

impl From<test::TestError> for UserError {
    fn from(error: test::TestError) -> Self {
        match error {
            test::TestError::BindParserError(error) => UserError::from(error),
            test::TestError::DeviceSpecParserError(error) => UserError::from(error),
            test::TestError::DebuggerError(error) => UserError::from(error),
            test::TestError::CompilerError(error) => UserError::from(error),
            test::TestError::InvalidSchema => {
                UserError::new("E401", "The test specification JSON schema is invalid.", None, true)
            }
            test::TestError::InvalidJsonError => UserError::new(
                "E402",
                "The test specification is invalid according to the schema.",
                None,
                false,
            ),
            test::TestError::JsonParserError(error) => {
                UserError::new("E403", &format!("Failed to parse JSON: {}.", error), None, false)
            }
        }
    }
}

impl From<LinterError> for UserError {
    fn from(error: LinterError) -> Self {
        match error {
            LinterError::LibraryNameMustNotContainUnderscores(name) => UserError::new(
                "E501",
                &format!("Library names should not contain underscores: `{}`.", name),
                None,
                false,
            ),
        }
    }
}
