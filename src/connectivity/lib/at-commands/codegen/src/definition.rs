// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug, Clone)]
pub enum Definition {
    Command(Command),
    Response { command_name: String, is_extension: bool, arguments: Arguments },
    Enum { name: String, variants: Vec<Variant> },
}

#[derive(Debug, Clone)]
pub enum Command {
    Execute { command_name: String, is_extension: bool, arguments: Option<ExecuteArguments> },
    Read { command_name: String, is_extension: bool },
    Test { command_name: String, is_extension: bool },
}

#[derive(Debug, Clone)]
pub struct ExecuteArguments {
    pub nonstandard_delimiter: Option<String>,
    pub arguments: Arguments,
}

#[derive(Debug, Clone)]
pub enum Arguments {
    ParenthesisDelimitedArgumentLists(Vec<Vec<Argument>>),
    ArgumentList(Vec<Argument>),
}

#[derive(Debug, Clone)]
pub struct Argument {
    pub identifier: String,
    pub typ: Type,
}

#[derive(Debug, Clone)]
pub enum Type {
    List(PrimitiveType),
    Map { key: PrimitiveType, value: PrimitiveType },
    PrimitiveType(PrimitiveType),
}

#[derive(Debug, Clone)]
pub enum PrimitiveType {
    String,
    Integer,
    // Special case 1 and 0 representing true and false
    BoolAsInt,
    NamedType(String),
}

#[derive(Debug, Clone)]
pub struct Variant {
    pub name: String,
    pub value: i64,
}
