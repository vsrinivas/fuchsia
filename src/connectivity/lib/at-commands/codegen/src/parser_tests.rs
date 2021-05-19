// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use codegen_lib::{
    definition::{
        Argument, Arguments, Command, Definition, DelimitedArguments, PrimitiveType, Type, Variant,
    },
    parser,
};

#[test]
fn parse_empty() {
    let string = String::from("");
    let parse_result = parser::parse(&string).unwrap();
    let expected_result = Vec::new();
    assert_eq!(parse_result, expected_result);
}

#[test]
fn parse_fail() {
    let string = String::from("unparseable");
    let parse_result = parser::parse(&string);
    assert!(parse_result.is_err());
}

#[test]
fn parse_multiple() {
    let string = String::from("command { AT+TESTONE } command { AT+TESTTWO }");
    let parse_result = parser::parse(&string).unwrap();
    let expected_result = vec![
        Definition::Command(Command::Execute {
            name: String::from("TESTONE"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![]),
                terminator: None,
            },
        }),
        Definition::Command(Command::Execute {
            name: String::from("TESTTWO"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![]),
                terminator: None,
            },
        }),
    ];
    assert_eq!(parse_result, expected_result);
}

fn test_parse(str_to_parse: &str, expected_result: Definition) {
    let parse_result = parser::parse(&String::from(str_to_parse)).unwrap();
    assert_eq!(vec![expected_result], parse_result);
}

// Execute command with no arguments
#[test]
fn exec_no_args() {
    test_parse(
        "command { ATTEST }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: false,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![]),
                terminator: None,
            },
        }),
    )
}

// Command with custom type name
#[test]
fn exec_with_type_name() {
    test_parse(
        "command TestName { ATTESTN }",
        Definition::Command(Command::Execute {
            name: String::from("TESTN"),
            type_name: Some(String::from("TestName")),
            is_extension: false,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![]),
                terminator: None,
            },
        }),
    )
}

// Extension execute command with no arguments
#[test]
fn exec_ext_no_args() {
    test_parse(
        "command { AT+TEST }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![]),
                terminator: None,
            },
        }),
    )
}

// Extension execute command with one argument, no trailing comma
#[test]
fn exec_one_arg_no_comma() {
    test_parse(
        "command { AT+TEST=field: Integer }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]),
                terminator: None,
            },
        }),
    )
}

// Extension execute command with one argument, with trailing comma
#[test]
fn exec_one_arg_no_with_comma() {
    test_parse(
        "command { AT+TEST=field: Integer, }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]),
                terminator: None,
            },
        }),
    )
}

// Extension execute command with one argument, > delimiter
#[test]
fn exec_one_arg_nonstd_delim() {
    test_parse(
        "command { AT+TEST>field: Integer }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(">")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]),
                terminator: None,
            },
        }),
    )
}

// Extension execute command with one argument, ; terminator
#[test]
fn exec_one_arg_term() {
    test_parse(
        "command { AT+TEST=field: Integer; }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]),
                terminator: Some(String::from(";")),
            },
        }),
    )
}

// Extension execute command with one argument, > delimiter, ; terminator
#[test]
fn exec_one_arg_nonstd_delim_term() {
    test_parse(
        "command { AT+TEST>field: Integer; }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(">")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]),
                terminator: Some(String::from(";")),
            },
        }),
    )
}

// Extension execute command with one argument, no delimiter
#[test]
fn exec_one_arg_no_delim() {
    test_parse(
        "command { AT+TESTfield: Integer }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: None,
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]),
                terminator: None,
            },
        }),
    )
}

// Extension execute command with multiple arguments, no trailing comma
#[test]
fn exec_args_no_comma() {
    test_parse(
        "command { AT+TEST=field1: Integer, field2: String }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![
                    Argument {
                        name: String::from("field1"),
                        typ: Type::PrimitiveType(PrimitiveType::Integer),
                    },
                    Argument {
                        name: String::from("field2"),
                        typ: Type::PrimitiveType(PrimitiveType::String),
                    },
                ]),
                terminator: None,
            },
        }),
    )
}

// Extension execute command with multiple arguments, with trailing comma
#[test]
fn exec_args_with_comma() {
    test_parse(
        "command { AT+TEST=field1: Integer, field2: String, }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![
                    Argument {
                        name: String::from("field1"),
                        typ: Type::PrimitiveType(PrimitiveType::Integer),
                    },
                    Argument {
                        name: String::from("field2"),
                        typ: Type::PrimitiveType(PrimitiveType::String),
                    },
                ]),
                terminator: None,
            },
        }),
    )
}

// Paren delimited argument list
#[test]
fn paren_args() {
    test_parse(
        "command { AT+TEST=(field: Integer) }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]]),
                terminator: None,
            },
        }),
    )
}

// Paren delimited multiple argument lists
#[test]
fn multiple_paren_args() {
    test_parse(
        "command { AT+TEST=(field1: Integer)(field2: Integer, field3: String) }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![
                    vec![Argument {
                        name: String::from("field1"),
                        typ: Type::PrimitiveType(PrimitiveType::Integer),
                    }],
                    vec![
                        Argument {
                            name: String::from("field2"),
                            typ: Type::PrimitiveType(PrimitiveType::Integer),
                        },
                        Argument {
                            name: String::from("field3"),
                            typ: Type::PrimitiveType(PrimitiveType::String),
                        },
                    ],
                ]),
                terminator: None,
            },
        }),
    )
}

// Option type argument
#[test]
fn option_type() {
    test_parse(
        "command { AT+TEST=field: Option<Integer>}",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::Option(PrimitiveType::Integer),
                }]),
                terminator: None,
            },
        }),
    )
}
// List type argument
#[test]
fn list_type() {
    test_parse(
        "command { AT+TEST=field: List<Integer>}",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::List(PrimitiveType::Integer),
                }]),
                terminator: None,
            },
        }),
    )
}

// Map type arguments
#[test]
fn map_type() {
    test_parse(
        "command { AT+TEST=field: Map<Integer, String> }",
        Definition::Command(Command::Execute {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from("=")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::Map { key: PrimitiveType::Integer, value: PrimitiveType::String },
                }]),
                terminator: None,
            },
        }),
    )
}

// Read command
#[test]
fn read() {
    test_parse(
        "command { ATTEST? }",
        Definition::Command(Command::Read {
            name: String::from("TEST"),
            type_name: None,
            is_extension: false,
        }),
    )
}

// Extension read command
#[test]
fn read_ext() {
    test_parse(
        "command { AT+TEST? }",
        Definition::Command(Command::Read {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
        }),
    )
}

// Test command
#[test]
fn test() {
    test_parse(
        "command { ATTEST=? }",
        Definition::Command(Command::Test {
            name: String::from("TEST"),
            type_name: None,
            is_extension: false,
        }),
    )
}

// Extension test command
#[test]
fn test_ext() {
    test_parse(
        "command { AT+TEST=? }",
        Definition::Command(Command::Test {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
        }),
    )
}

// Response with no arguments
#[test]
fn resp_no_args() {
    test_parse(
        "response { TEST: }",
        Definition::Response {
            name: String::from("TEST"),
            type_name: None,
            is_extension: false,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ArgumentList(Vec::new()),
                terminator: None,
            },
        },
    )
}

// Response with custom type name
#[test]
fn resp_no_args_custom_typename() {
    test_parse(
        "response TestResponse { TESTN: }",
        Definition::Response {
            name: String::from("TESTN"),
            type_name: Some(String::from("TestResponse")),
            is_extension: false,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ArgumentList(Vec::new()),
                terminator: None,
            },
        },
    )
}

// Extension response command with no arguments
#[test]
fn resp_ext_no_args() {
    test_parse(
        "response { +TEST: }",
        Definition::Response {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ArgumentList(Vec::new()),
                terminator: None,
            },
        },
    )
}

// Extension response with one argument, no trailing comma
#[test]
fn resp_one_arg_no_comma() {
    test_parse(
        "response { +TEST: field: Integer }",
        Definition::Response {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]),
                terminator: None,
            },
        },
    )
}

// Extension response with one argument, with trailing comma
#[test]
fn resp_one_arg_with_comma() {
    test_parse(
        "response { +TEST: field: Integer, }",
        Definition::Response {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ArgumentList(vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]),
                terminator: None,
            },
        },
    )
}

// Extension response with multiple arguments, no trailing comma
#[test]
fn resp_args_no_comma() {
    test_parse(
        "response { +TEST: field1: Integer, field2: Integer }",
        Definition::Response {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ArgumentList(vec![
                    Argument {
                        name: String::from("field1"),
                        typ: Type::PrimitiveType(PrimitiveType::Integer),
                    },
                    Argument {
                        name: String::from("field2"),
                        typ: Type::PrimitiveType(PrimitiveType::Integer),
                    },
                ]),
                terminator: None,
            },
        },
    )
}

// Extension response with multiple arguments, with trailing comma
#[test]
fn resp_args_with_comma() {
    test_parse(
        "response { +TEST: field1: Integer, field2: Integer,}",
        Definition::Response {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ArgumentList(vec![
                    Argument {
                        name: String::from("field1"),
                        typ: Type::PrimitiveType(PrimitiveType::Integer),
                    },
                    Argument {
                        name: String::from("field2"),
                        typ: Type::PrimitiveType(PrimitiveType::Integer),
                    },
                ]),
                terminator: None,
            },
        },
    )
}

// Response with paren delimited argument list
#[test]
fn resp_paren_args() {
    test_parse(
        "response { +TEST: (field: Integer) }",
        Definition::Response {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![vec![Argument {
                    name: String::from("field"),
                    typ: Type::PrimitiveType(PrimitiveType::Integer),
                }]]),
                terminator: None,
            },
        },
    )
}

// Response with paren delimited multiple argment lists
#[test]
fn resp_paren_multiple_args() {
    test_parse(
        "response { +TEST: (field1: Integer)(field2: Integer, field3: String) }",
        Definition::Response {
            name: String::from("TEST"),
            type_name: None,
            is_extension: true,
            arguments: DelimitedArguments {
                delimiter: Some(String::from(":")),
                arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![
                    vec![Argument {
                        name: String::from("field1"),
                        typ: Type::PrimitiveType(PrimitiveType::Integer),
                    }],
                    vec![
                        Argument {
                            name: String::from("field2"),
                            typ: Type::PrimitiveType(PrimitiveType::Integer),
                        },
                        Argument {
                            name: String::from("field3"),
                            typ: Type::PrimitiveType(PrimitiveType::String),
                        },
                    ],
                ]),
                terminator: None,
            },
        },
    )
}

// Enum with one variant
#[test]
fn enum_one_variant_no_comma() {
    test_parse(
        "enum Test { Variant1 = 1 }",
        Definition::Enum {
            name: String::from("Test"),
            variants: vec![Variant { name: String::from("Variant1"), value: 1 }],
        },
    )
}

// Enum with one variant, trailing comma
#[test]
fn enum_one_variant_with_comma() {
    test_parse(
        "enum Test { Variant1 = 1, }",
        Definition::Enum {
            name: String::from("Test"),
            variants: vec![Variant { name: String::from("Variant1"), value: 1 }],
        },
    )
}

// Enum with multiple variants
#[test]
fn enum_variants_no_comma() {
    test_parse(
        "enum Test { Variant1 = 1, Variant2 = 2 }",
        Definition::Enum {
            name: String::from("Test"),
            variants: vec![
                Variant { name: String::from("Variant1"), value: 1 },
                Variant { name: String::from("Variant2"), value: 2 },
            ],
        },
    )
}

// Enum with multiple variants, trailing comma
#[test]
fn enum_variants_with_comma() {
    test_parse(
        "enum Test { Variant1 = 1, Variant2 = 2, }",
        Definition::Enum {
            name: String::from("Test"),
            variants: vec![
                Variant { name: String::from("Variant1"), value: 1 },
                Variant { name: String::from("Variant2"), value: 2 },
            ],
        },
    )
}
