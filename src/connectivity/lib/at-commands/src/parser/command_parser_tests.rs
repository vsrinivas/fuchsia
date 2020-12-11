// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Tests for the AT command parser.
use crate::{
    lowlevel::{
        arguments::{Argument, Arguments, PrimitiveArgument},
        command::{Command, ExecuteArguments},
    },
    parser::command_parser,
};

#[test]
fn parse_empty() {
    let string = String::from("");
    let parse_result = command_parser::parse(&string);
    assert!(parse_result.is_err());
}

#[test]
fn parse_fail() {
    let string = String::from("unparseable");
    let parse_result = command_parser::parse(&string);
    assert!(parse_result.is_err());
}

fn test_parse(str_to_parse: &str, expected_result: Command) {
    let parse_result = command_parser::parse(&String::from(str_to_parse)).unwrap();
    assert_eq!(expected_result, parse_result);
}

// Execute command with no arguments
#[test]
fn exec_no_args() {
    test_parse(
        "ATTEST a a",
        Command::Execute { name: String::from("TEST"), is_extension: false, arguments: None },
    )
}

// Extension execute command with no arguments
#[test]
fn exec_ext_no_args() {
    test_parse(
        "AT+TEST",
        Command::Execute { name: String::from("TEST"), is_extension: true, arguments: None },
    )
}

// Extension execute command with one integer argument, no trailing comma
#[test]
fn exec_one_int_arg_no_comma() {
    test_parse(
        "AT+TEST=1",
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
    )
}

// Extension execute command with one integer argument and a > delimiter
#[test]
fn exec_one_int_arg_nonstandard_delimiter() {
    test_parse(
        "AT+TEST>1",
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
    )
}
// Extension execute command with one string argument, no trailing comma
#[test]
fn exec_one_string_arg_no_comma() {
    test_parse(
        "AT+TEST=abc",
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
    )
}

// Extension execute command with one key-value argument, no trailing comma
#[test]
fn exec_one_kv_arg_no_comma() {
    test_parse(
        "AT+TEST=1=abc",
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
    )
}
// Extension execute command with one integer argument, with trailing comma
#[test]
fn exec_one_int_arg_with_comma() {
    test_parse(
        "AT+TEST=1,",
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
    )
}

// Extension execute command with one string argument, with trailing comma
#[test]
fn exec_one_string_arg_with_comma() {
    test_parse(
        "AT+TEST=abc,",
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
    )
}

// Extension execute command with one key-value argument, with trailing comma
#[test]
fn exec_one_kv_arg_with_comma() {
    test_parse(
        "AT+TEST=abc=1,",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Some(ExecuteArguments {
                nonstandard_delimiter: None,
                arguments: Arguments::ArgumentList(vec![Argument::KeyValueArgument {
                    key: PrimitiveArgument::String(String::from("abc")),
                    value: PrimitiveArgument::Integer(1),
                }]),
            }),
        },
    )
}

// Extension execute command with multiple arguments, no trailing comma
#[test]
fn exec_args_no_comma() {
    test_parse(
        "AT+TEST=abc,1",
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
    )
}

// Extension execute command with multiple arguments with trailing comma
#[test]
fn exec_args_with_comma() {
    test_parse(
        "AT+TEST=abc,1,",
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
    )
}

// Paren delimited argument list
#[test]
fn paren_args() {
    test_parse(
        "AT+TEST=(1)",
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
    )
}

// Paren delimited multiple argument lists
#[test]
fn multiple_paren_args() {
    test_parse(
        "AT+TEST=(1)(2,abc)",
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
    )
}

// Paren delimited multiple argument lists with key-value elements
#[test]
fn multiple_paren_kv_args() {
    test_parse(
        "AT+TEST=(1=abc)(2,xyz=3)",
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
    )
}

// Read command
#[test]
fn read() {
    test_parse("ATTEST?", Command::Read { name: String::from("TEST"), is_extension: false })
}

// Extension read command
#[test]
fn read_ext() {
    test_parse("AT+TEST?", Command::Read { name: String::from("TEST"), is_extension: true })
}

// Test command
#[test]
fn test() {
    test_parse("ATTEST=?", Command::Test { name: String::from("TEST"), is_extension: false })
}

// Extension test command
#[test]
fn test_ext() {
    test_parse("AT+TEST=?", Command::Test { name: String::from("TEST"), is_extension: true })
}
