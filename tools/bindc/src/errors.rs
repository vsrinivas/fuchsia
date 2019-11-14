// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler::CompilerError;
use crate::dependency_graph::DependencyError;
use crate::parser_common::{BindParserError, CompoundIdentifier};
use std::fmt;

pub struct UserError {
    index: String,
    message: String,
    span: Option<String>,
}

impl UserError {
    fn new(index: &str, message: &str, span: Option<String>) -> Self {
        UserError { index: index.to_string(), message: message.to_string(), span: span }
    }
}

impl fmt::Display for UserError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "[{}]: {}", self.index, self.message)?;
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
                UserError::new("E001", "Expected a type keyword.", Some(span))
            }
            BindParserError::StringLiteral(span) => {
                UserError::new("E002", "Expected a string literal.", Some(span))
            }
            BindParserError::NumericLiteral(span) => {
                UserError::new("E003", "Expected a numerical literal.", Some(span))
            }
            BindParserError::BoolLiteral(span) => {
                UserError::new("E004", "Expected a boolean literal.", Some(span))
            }
            BindParserError::Identifier(span) => {
                UserError::new("E005", "Expected an identifier.", Some(span))
            }
            BindParserError::Semicolon(span) => {
                UserError::new("E006", "Missing semicolon (;).", Some(span))
            }
            BindParserError::Assignment(span) => {
                UserError::new("E007", "Expected an assignment. Are you missing a '='?", Some(span))
            }
            BindParserError::ListStart(span) => {
                UserError::new("E008", "Expected a list. Are you missing a '{'?", Some(span))
            }
            BindParserError::ListEnd(span) => UserError::new(
                "E009",
                "List does not terminate. Are you missing a '}'?",
                Some(span),
            ),
            BindParserError::ListSeparator(span) => UserError::new(
                "E010",
                "Expected a list separator. Are you missing a ','?",
                Some(span),
            ),
            BindParserError::LibraryKeyword(span) => {
                UserError::new("E011", "Expected 'library' keyword.", Some(span))
            }
            BindParserError::UsingKeyword(span) => {
                UserError::new("E012", "Expected 'using' keyword.", Some(span))
            }
            BindParserError::AsKeyword(span) => {
                UserError::new("E013", "Expected 'as' keyword", Some(span))
            }
            BindParserError::IfBlockStart(span) => {
                UserError::new("E014", "Expected '{' to begin if statement block.", Some(span))
            }
            BindParserError::IfBlockEnd(span) => {
                UserError::new("E015", "Expected '}' to end if statement block.", Some(span))
            }
            BindParserError::IfKeyword(span) => {
                UserError::new("E016", "Expected 'if' keyword.", Some(span))
            }
            BindParserError::ElseKeyword(span) => {
                UserError::new("E017", "Expected 'else' keyword", Some(span))
            }
            BindParserError::ConditionOp(span) => {
                UserError::new("E018", "Expected a condition operation ('==' or '!='.)", Some(span))
            }
            BindParserError::ConditionValue(span) => UserError::new(
                "E019",
                "Expected a condition value: string, number, boolean, or identifier",
                Some(span),
            ),
            BindParserError::AcceptKeyword(span) => {
                UserError::new("E020", "Expected 'accept' keyword.", Some(span))
            }
            BindParserError::NoStatements(span) => UserError::new(
                "E021",
                "Bind programs must contain at least one statement.",
                Some(span),
            ),
            BindParserError::Unknown(span, kind) => UserError::new(
                "E022",
                &format!(
                    "Unexpected parser error: {:?}. This is a bind compiler bug, please report it!",
                    kind
                ),
                Some(span),
            ),
        }
    }
}

impl From<CompilerError> for UserError {
    fn from(error: CompilerError) -> Self {
        match error {
            CompilerError::FileOpenError(path) => UserError::new(
                "E100",
                &format!("Failed to open file: {}.", path.to_string_lossy()),
                None,
            ),
            CompilerError::FileReadError(path) => UserError::new(
                "E101",
                &format!("Failed to read file: {}.", path.to_string_lossy()),
                None,
            ),
            CompilerError::BindParserError(error) => UserError::from(error),
            CompilerError::DependencyError(error) => UserError::from(error),
            CompilerError::DuplicateIdentifier(identifier) => UserError::new(
                "E102",
                &format!("The identifier `{}` is defined multiple times.", identifier),
                None,
            ),
            CompilerError::TypeMismatch(identifier) => UserError::new(
                "E103",
                &format!("The identifier `{}` is extended with a mismatched type.", identifier),
                None,
            ),
            CompilerError::UnresolvedQualification(identifier) => UserError::new(
                "E104",
                &format!("Could not resolve the qualifier on `{}`.", identifier),
                None,
            ),
            CompilerError::UndeclaredKey(identifier) => UserError::new(
                "E105",
                &format!("Could not find previous definition of extended key `{}`.", identifier),
                None,
            ),
            CompilerError::MissingExtendsKeyword(identifier) => UserError::new(
                "E106",
                &format!(
                    "Cannot define a key with a namespace: `{}`. Are you missing an `extend`?",
                    identifier
                ),
                None,
            ),
            CompilerError::InvalidExtendsKeyword(identifier) => UserError::new(
                "E107",
                &format!("Cannot extend a key with no namespace: `{}`.", identifier),
                None,
            ),
            CompilerError::UnknownKey(identifier) => UserError::new(
                "E108",
                &format!("Bind program refers to undefined identifier: `{}`.", identifier),
                None,
            ),
            CompilerError::IfStatementMustBeTerminal => {
                UserError::new("E109", "If statements must be the last statement in a block", None)
            }
        }
    }
}

impl From<DependencyError<CompoundIdentifier>> for UserError {
    fn from(error: DependencyError<CompoundIdentifier>) -> Self {
        match error {
            DependencyError::MissingDependency(library) => {
                UserError::new("E200", &format!("Missing dependency: {}", library), None)
            }
            DependencyError::CircularDependency => {
                UserError::new("E201", "Cicular dependency", None)
            }
        }
    }
}
