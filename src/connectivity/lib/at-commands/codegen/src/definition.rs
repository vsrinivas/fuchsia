// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::codegen::common::to_initial_capital;

#[derive(Debug, Clone, PartialEq)]
pub enum Definition {
    Command(Command),
    Response { name: String, type_name: Option<String>, is_extension: bool, arguments: Arguments },
    Enum { name: String, variants: Vec<Variant> },
}

#[derive(Debug, Clone, PartialEq)]
pub enum Command {
    Execute {
        name: String,
        type_name: Option<String>,
        is_extension: bool,
        arguments: Option<ExecuteArguments>,
    },
    Read {
        name: String,
        type_name: Option<String>,
        is_extension: bool,
    },
    Test {
        name: String,
        type_name: Option<String>,
        is_extension: bool,
    },
}

impl Command {
    pub fn type_name(&self) -> String {
        match self {
            Command::Execute {
                name,
                type_name,
                arguments: Some(ExecuteArguments { nonstandard_delimiter: Some(_), .. }),
                ..
            } => {
                type_name.clone().unwrap_or_else(|| format!("{}Special", to_initial_capital(name)))
            }
            Command::Execute { name, type_name, .. } => {
                type_name.clone().unwrap_or_else(|| to_initial_capital(name.as_str()))
            }
            Command::Read { name, type_name, .. } => type_name
                .clone()
                .unwrap_or_else(|| format!("{}Read", to_initial_capital(name.as_str()))),
            Command::Test { name, type_name, .. } => type_name
                .clone()
                .unwrap_or_else(|| format!("{}Test", to_initial_capital(name.as_str()))),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct ExecuteArguments {
    pub nonstandard_delimiter: Option<String>,
    pub arguments: Arguments,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Arguments {
    ParenthesisDelimitedArgumentLists(Vec<Vec<Argument>>),
    ArgumentList(Vec<Argument>),
}

impl Arguments {
    pub fn is_empty(&self) -> bool {
        match self {
            Self::ArgumentList(vec) => vec.is_empty(),
            Self::ParenthesisDelimitedArgumentLists(vec) => {
                vec.is_empty() || vec.into_iter().all(|el| el.is_empty())
            }
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Argument {
    pub name: String,
    pub typ: Type,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Type {
    List(PrimitiveType),
    Map { key: PrimitiveType, value: PrimitiveType },
    PrimitiveType(PrimitiveType),
}

#[derive(Debug, Clone, PartialEq)]
pub enum PrimitiveType {
    String,
    Integer,
    // Special case 1 and 0 representing true and false
    BoolAsInt,
    NamedType(String),
}

#[derive(Debug, Clone, PartialEq)]
pub struct Variant {
    pub name: String,
    pub value: i64,
}
