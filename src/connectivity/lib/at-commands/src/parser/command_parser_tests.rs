// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Tests for the AT command parser.
use crate::{
    lowlevel::{Argument, Arguments, Command, DelimitedArguments},
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
        "ATTEST",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: false,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![]),
                terminator: None,
            },
        },
    )
}

// Extension execute command with no arguments
#[test]
fn exec_ext_no_args() {
    test_parse(
        "AT+TEST",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![]),
                terminator: None,
            },
        },
    )
}

// Extension execute command with one argument
#[test]
fn exec_one_int_arg_no_comma() {
    test_parse(
        "AT+TEST=1",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                    String::from("1"),
                )]),
                terminator: None,
            },
        },
    )
}

// Extension execute command with one argument and no delimiter
#[test]
fn exec_one_int_arg_no_delimiter() {
    test_parse(
        "AT+TEST1",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                    String::from("1"),
                )]),
                terminator: None,
            },
        },
    )
}

// Extension execute command with one argument and a > delimiter
#[test]
fn exec_one_int_arg_nonstandard_delimiter() {
    test_parse(
        "AT+TEST>1",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(">")),
                arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                    String::from("1"),
                )]),
                terminator: None,
            },
        },
    )
}

// Extension execute command with one integer argument and a ; terminator
#[test]
fn exec_one_int_arg_terminator() {
    test_parse(
        "AT+TEST=1;",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                    String::from("1"),
                )]),
                terminator: Some(String::from(";")),
            },
        },
    )
}

// Extension execute command with one integer argument, a > delimiter and a ; terminator
#[test]
fn exec_one_int_arg_nonstandard_delimiter_terminator() {
    test_parse(
        "AT+TEST>1;",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(">")),
                arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                    String::from("1"),
                )]),
                terminator: Some(String::from(";")),
            },
        },
    )
}

// Extension execute command with one key-value argument
#[test]
fn exec_one_kv_arg_no_comma() {
    test_parse(
        "AT+TEST=1=abc",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument::KeyValueArgument {
                    key: String::from("1"),
                    value: String::from("abc"),
                }]),
                terminator: None,
            },
        },
    )
}

// Extension execute command with multiple arguments
#[test]
fn exec_args_no_comma() {
    test_parse(
        "AT+TEST=abc,1",
        Command::Execute {
            name: String::from("TEST"),
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![
                    Argument::PrimitiveArgument(String::from("abc")),
                    Argument::PrimitiveArgument(String::from("1")),
                ]),
                terminator: None,
            },
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
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![vec![
                    Argument::PrimitiveArgument(String::from("1")),
                ]]),
                terminator: None,
            },
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
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![
                    vec![Argument::PrimitiveArgument(String::from("1"))],
                    vec![
                        Argument::PrimitiveArgument(String::from("2")),
                        Argument::PrimitiveArgument(String::from("abc")),
                    ],
                ]),
                terminator: None,
            },
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
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![
                    vec![Argument::KeyValueArgument {
                        key: String::from("1"),
                        value: String::from("abc"),
                    }],
                    vec![
                        Argument::PrimitiveArgument(String::from("2")),
                        Argument::KeyValueArgument {
                            key: String::from("xyz"),
                            value: String::from("3"),
                        },
                    ],
                ]),
                terminator: None,
            },
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
