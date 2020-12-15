// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the AT command AST.
#![cfg(test)]

use crate::lowlevel::{
    arguments::{Argument, Arguments, PrimitiveArgument},
    command::{Command, ExecuteArguments},
    write_to::WriteTo,
};

fn cr_terminate(str: &str) -> String {
    format!("{}\r", str)
}

fn test_write(command_to_serialize: Command, expected_string: String) {
    let mut sink = Vec::new();
    assert!(command_to_serialize.write_to(&mut sink).is_ok());
    // Convert to a String so errors are human readable, not just hex.
    let actual_string = String::from_utf8(sink).unwrap();
    assert_eq!(expected_string, actual_string);
}

// Execute command with no arguments
#[test]
fn exec_no_args() {
    test_write(
        Command::Execute { name: String::from("TEST"), is_extension: false, arguments: None },
        cr_terminate("ATTEST"),
    )
}

// Extension execute command with no arguments
#[test]
fn exec_ext_no_args() {
    test_write(
        Command::Execute { name: String::from("TEST"), is_extension: true, arguments: None },
        cr_terminate("AT+TEST"),
    )
}

// Extension execute command with one integer argument
#[test]
fn exec_one_int_arg() {
    test_write(
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: None,
                arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                    PrimitiveArgument::Integer(1),
                )]),
            }),
        },
        cr_terminate("AT+TEST=1"),
    )
}

// Extension execute command with one integer argument and a > delimiter
#[test]
fn exec_one_int_arg_nonstandard_delimiter() {
    test_write(
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: Some(String::from(">")),
                arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                    PrimitiveArgument::Integer(1),
                )]),
            }),
        },
        cr_terminate("AT+TEST>1"),
    )
}
// Extension execute command with one string argument
#[test]
fn exec_one_string_arg() {
    test_write(
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: None,
                arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                    PrimitiveArgument::String(String::from("abc")),
                )]),
            }),
        },
        cr_terminate("AT+TEST=abc"),
    )
}

// Extension execute command with one key-value argument
#[test]
fn exec_one_kv_arg() {
    test_write(
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: None,
                arguments: Arguments::ArgumentList(vec![Argument::KeyValueArgument {
                    key: PrimitiveArgument::Integer(1),
                    value: PrimitiveArgument::String(String::from("abc")),
                }]),
            }),
        },
        cr_terminate("AT+TEST=1=abc"),
    )
}

// Extension execute command with multiple arguments
#[test]
fn exec_args() {
    test_write(
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: None,
                arguments: Arguments::ArgumentList(vec![
                    Argument::PrimitiveArgument(PrimitiveArgument::String(String::from("abc"))),
                    Argument::PrimitiveArgument(PrimitiveArgument::Integer(1)),
                ]),
            }),
        },
        cr_terminate("AT+TEST=abc,1"),
    )
}

// Paren delimited argument list
#[test]
fn paren_args() {
    test_write(
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: None,
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![vec![
                    Argument::PrimitiveArgument(PrimitiveArgument::Integer(1)),
                ]]),
            }),
        },
        cr_terminate("AT+TEST=(1)"),
    )
}

// Paren delimited multiple argument lists
#[test]
fn multiple_paren_args() {
    test_write(
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: None,
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![
                    vec![Argument::PrimitiveArgument(PrimitiveArgument::Integer(1))],
                    vec![
                        Argument::PrimitiveArgument(PrimitiveArgument::Integer(2)),
                        Argument::PrimitiveArgument(PrimitiveArgument::String(String::from("abc"))),
                    ],
                ]),
            }),
        },
        cr_terminate("AT+TEST=(1)(2,abc)"),
    )
}

// Paren delimited multiple argument lists with key-value elements
#[test]
fn multiple_paren_kv_args() {
    test_write(
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: None,
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![
                    vec![Argument::KeyValueArgument {
                        key: PrimitiveArgument::Integer(1),
                        value: PrimitiveArgument::String(String::from("abc")),
                    }],
                    vec![
                        Argument::PrimitiveArgument(PrimitiveArgument::Integer(2)),
                        Argument::KeyValueArgument {
                            key: PrimitiveArgument::String(String::from("xyz")),
                            value: PrimitiveArgument::Integer(3),
                        },
                    ],
                ]),
            }),
        },
        cr_terminate("AT+TEST=(1=abc)(2,xyz=3)"),
    )
}

// Read command
#[test]
fn read() {
    test_write(
        Command::Read { name: String::from("TEST"), is_extension: false },
        cr_terminate("ATTEST?"),
    )
}

// Extension read command
#[test]
fn read_ext() {
    test_write(
        Command::Read { name: String::from("TEST"), is_extension: true },
        cr_terminate("AT+TEST?"),
    )
}

// Test command
#[test]
fn test() {
    test_write(
        Command::Test { name: String::from("TEST"), is_extension: false },
        cr_terminate("ATTEST=?"),
    )
}

// Extension test command
#[test]
fn test_ext() {
    test_write(
        Command::Test { name: String::from("TEST"), is_extension: true },
        cr_terminate("AT+TEST=?"),
    )
}
