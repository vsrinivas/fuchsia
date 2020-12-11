// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module contains an AST for arguments for both AT commands and responses.
/// The format of of these is not specifed in any one place in the spec, but they are
/// described thoughout HFP 1.8.

/// The collection of arguments to a given command or response.
///
/// AT supports multiple different formats, represented here by the different enum
/// branches.

#[derive(Debug, Clone, PartialEq)]
pub enum Arguments {
    /// A sequence of multiple arguments lists delimited by parentheses, like, `(1,2)(3,4)(a=1)`
    ParenthesisDelimitedArgumentLists(Vec<Vec<Argument>>),
    /// A single argument list delimited by commas, like, `1,2,a=3`
    ArgumentList(Vec<Argument>),
}

/// An individual argument in a list
#[derive(Debug, Clone, PartialEq)]
pub enum Argument {
    /// A primitive string or int.
    PrimitiveArgument(PrimitiveArgument),
    /// A key-value pair like `a=1`
    KeyValueArgument { key: PrimitiveArgument, value: PrimitiveArgument },
}

/// Primitive string or int arguments.
#[derive(Debug, Clone, PartialEq)]
pub enum PrimitiveArgument {
    String(String),
    Integer(i64),
}
