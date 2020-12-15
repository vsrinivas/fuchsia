// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for AT command responses.
#![cfg(test)]

use crate::lowlevel::{
    arguments::{Argument, Arguments, PrimitiveArgument},
    response::{HardcodedError, Response},
    write_to::WriteTo,
};

fn cr_lf_delimit(str: &str) -> String {
    format!("\r\n{}\r\n", str)
}

fn test_write(response_to_serialize: Response, expected_string: String) {
    let mut sink = Vec::new();
    assert!(response_to_serialize.write_to(&mut sink).is_ok());
    // Convert to a String so errors are human readable, not just hex.
    let actual_string = String::from_utf8(sink).unwrap();
    assert_eq!(expected_string, actual_string);
}

// Ok response
#[test]
fn ok() {
    test_write(Response::Ok, cr_lf_delimit("OK"))
}

// Error response
#[test]
fn error() {
    test_write(Response::Error, cr_lf_delimit("ERROR"))
}

// Hardcoded error response NO CARRIER
#[test]
fn no_carrier() {
    test_write(Response::HardcodedError(HardcodedError::NoCarrier), cr_lf_delimit("NO CARRIER"))
}

// Hardcoded error response BUSY
#[test]
fn busy() {
    test_write(Response::HardcodedError(HardcodedError::Busy), cr_lf_delimit("BUSY"))
}

// Hardcoded error response NO ANSWER
#[test]
fn no_answer() {
    test_write(Response::HardcodedError(HardcodedError::NoAnswer), cr_lf_delimit("NO ANSWER"))
}

// Hardcoded error response DELAYED
#[test]
fn delayed() {
    test_write(Response::HardcodedError(HardcodedError::Delayed), cr_lf_delimit("DELAYED"))
}

// Hardcoded error response BLACKLIST
#[test]
fn blacklist() {
    test_write(Response::HardcodedError(HardcodedError::Blacklist), cr_lf_delimit("BLACKLIST"))
}

// +CME error
#[test]
fn cme_error() {
    test_write(Response::CmeError(1), cr_lf_delimit("+CME ERROR: 1"))
}

// Response with no arguments
#[test]
fn no_args() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: false,
            arguments: Arguments::ArgumentList(vec![]),
        },
        cr_lf_delimit("TEST: "),
    )
}

// Extension response with no arguments
#[test]
fn ext_no_args() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Arguments::ArgumentList(vec![]),
        },
        cr_lf_delimit("+TEST: "),
    )
}

// Extension response with one integer argument
#[test]
fn one_int_arg() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                PrimitiveArgument::Integer(1),
            )]),
        },
        cr_lf_delimit("+TEST: 1"),
    )
}

// Extension response with one string argument
#[test]
fn one_string_arg() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Arguments::ArgumentList(vec![Argument::PrimitiveArgument(
                PrimitiveArgument::String(String::from("abc")),
            )]),
        },
        cr_lf_delimit("+TEST: abc"),
    )
}

// Extension response with one key-value argument
#[test]
fn one_kv_arg() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Arguments::ArgumentList(vec![Argument::KeyValueArgument {
                key: PrimitiveArgument::Integer(1),
                value: PrimitiveArgument::String(String::from("abc")),
            }]),
        },
        cr_lf_delimit("+TEST: 1=abc"),
    )
}

// Extension response with multiple arguments
#[test]
fn args() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Arguments::ArgumentList(vec![
                Argument::PrimitiveArgument(PrimitiveArgument::String(String::from("abc"))),
                Argument::PrimitiveArgument(PrimitiveArgument::Integer(1)),
            ]),
        },
        cr_lf_delimit("+TEST: abc,1"),
    )
}

// Paren delimited argument list
#[test]
fn paren_args() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![vec![
                Argument::PrimitiveArgument(PrimitiveArgument::Integer(1)),
            ]]),
        },
        cr_lf_delimit("+TEST: (1)"),
    )
}

// Paren delimited multiple argument lists
#[test]
fn multiple_paren_args() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: true,
            arguments: Arguments::ParenthesisDelimitedArgumentLists(vec![
                vec![Argument::PrimitiveArgument(PrimitiveArgument::Integer(1))],
                vec![
                    Argument::PrimitiveArgument(PrimitiveArgument::Integer(2)),
                    Argument::PrimitiveArgument(PrimitiveArgument::String(String::from("abc"))),
                ],
            ]),
        },
        cr_lf_delimit("+TEST: (1)(2,abc)"),
    )
}

// Paren delimited multiple argument lists with key-value elements
#[test]
fn multiple_paren_kv_args() {
    test_write(
        Response::Success {
            name: String::from("TEST"),
            is_extension: true,
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
        },
        cr_lf_delimit("+TEST: (1=abc)(2,xyz=3)"),
    )
}
