// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for AT command responses.
#![cfg(test)]

use {
    crate::{
        generated::translate, generated::types as highlevel, lowlevel, lowlevel::write_to::WriteTo,
        parser::response_parser, serde::SerDe,
    },
    std::{collections::HashMap, io::Cursor},
};

fn cr_lf_delimit(str: &str) -> String {
    format!("\r\n{}\r\n", str)
}

// This function tests that the raise, lower, parse and write funtions all compose and round trip as
// expected, and that the serde methods which compose them continue to work as well.  It takes a
// highlevel representation, a lowlevel representation and a string representation of the same AT
// command and tests that converting between them in all possible ways produce the expected values.
fn test_roundtrips(highlevel: highlevel::Response, lowlevel: lowlevel::Response, string: String) {
    // TEST I: highlevel -> lowlevel -> bytes -> lowlevel -> highlevel
    let mut bytes_from_lowlevel = Vec::new();

    // Do round trip
    let lowlevel_from_highlevel = translate::lower_response(&highlevel);
    lowlevel_from_highlevel.write_to(&mut bytes_from_lowlevel).expect("Failed to write lowlevel.");
    // TODO(fxb/66041) Convert parse to use Read rather than strings.
    let string_from_lowlevel =
        String::from_utf8(bytes_from_lowlevel).expect("Failed to convert bytes to UTF8.");
    let lowlevel_from_bytes =
        response_parser::parse(&string_from_lowlevel).expect("Failed to parse String.");
    let highlevel_from_lowlevel =
        translate::raise_response(&lowlevel_from_bytes).expect("Failed to raise lowlevel.");

    // Assert all the things are equal.
    assert_eq!(highlevel, highlevel_from_lowlevel);
    assert_eq!(lowlevel, lowlevel_from_highlevel);
    assert_eq!(lowlevel, lowlevel_from_bytes);
    assert_eq!(string, string_from_lowlevel);

    // TEST II: highlevel -> bytes -> highlevel
    // This should be identical to above assuming SerDe::serialize and
    // SerDe::deserialize are implemented correctly.
    let mut bytes_from_highlevel = Vec::new();

    // Do round trip
    highlevel.serialize(&mut bytes_from_highlevel).expect("Failed to serialize highlevel.");
    let highlevel_from_bytes =
        highlevel::Response::deserialize(&mut Cursor::new(bytes_from_highlevel.clone()))
            .expect("Failed to serialize bytes created by serializing highlevel.");

    // Convert to a String so errors are human readable, not just hex.
    let string_from_highlevel = String::from_utf8(bytes_from_highlevel)
        .expect("Failed to serialize bytes created by serializing highlevel.");

    // Assert all the things are equal.
    assert_eq!(highlevel, highlevel_from_bytes);
    assert_eq!(string, string_from_highlevel);

    // TEST III: bytes -> lowlevel -> highlevel -> lowlevel -> bytes
    let mut bytes_from_lowlevel = Vec::new();

    // Do round trip
    // TODO(fxb/66041) Convert parse to use Read rather than strings.
    let lowlevel_from_bytes = response_parser::parse(&string).expect("Failed to parse string.");
    let highlevel_from_lowlevel =
        translate::raise_response(&lowlevel_from_bytes).expect("Failed to raise lowlevel.");
    let lowlevel_from_highlevel = translate::lower_response(&highlevel_from_lowlevel);
    lowlevel_from_highlevel.write_to(&mut bytes_from_lowlevel).expect("Failed to write lowlevel.");

    // Convert to a String so errors are human readable, not just hex.
    let string_from_lowlevel =
        String::from_utf8(bytes_from_lowlevel).expect("Failed to convert bytes to UFT8.");

    // Assert all the things are equal.
    assert_eq!(highlevel, highlevel_from_lowlevel);
    assert_eq!(lowlevel, lowlevel_from_highlevel);
    assert_eq!(lowlevel, lowlevel_from_bytes);
    assert_eq!(string, string_from_lowlevel);

    // TEST IV: bytes -> highlevel -> bytes
    // This should be identical to above assuming SerDe::serialize and
    // SerDe::deserialize are implemented correctly.
    let mut bytes_from_highlevel = Vec::new();

    // Do round trip
    let highlevel_from_bytes = highlevel::Response::deserialize(&mut Cursor::new(string.clone()))
        .expect("Failed to deserialize string.");
    highlevel_from_bytes.serialize(&mut bytes_from_highlevel).expect("Failed to raise lowlevel.");

    // Convert to a String so errors are human readable, not just hex.
    let string_from_highlevel =
        String::from_utf8(bytes_from_highlevel).expect("Failed to convert bytes to UFT8.");

    // Assert all the things are equal.
    assert_eq!(highlevel, highlevel_from_bytes);
    assert_eq!(string, string_from_highlevel);
}

// TODO(fdb/70295) Add tests for nonsuccess cases, such as OK, ERROR,
// hardcoded responses like NO CARRIER and CME ERRORS.

// Response with no arguments
#[test]
fn no_args() {
    test_roundtrips(
        highlevel::Response::Test {},
        lowlevel::Response::Success {
            name: String::from("TEST"),
            is_extension: false,
            arguments: lowlevel::Arguments::ArgumentList(vec![]),
        },
        cr_lf_delimit("TEST: "),
    )
}

// Extension response with no arguments
#[test]
fn ext_no_args() {
    test_roundtrips(
        highlevel::Response::Testext {},
        lowlevel::Response::Success {
            name: String::from("TESTEXT"),
            is_extension: true,
            arguments: lowlevel::Arguments::ArgumentList(vec![]),
        },
        cr_lf_delimit("+TESTEXT: "),
    )
}

// Extension response with one integer argument
#[test]
fn one_int_arg() {
    test_roundtrips(
        highlevel::Response::Testi { field: 1 },
        lowlevel::Response::Success {
            name: String::from("TESTI"),
            is_extension: true,
            arguments: lowlevel::Arguments::ArgumentList(vec![
                lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(1)),
            ]),
        },
        cr_lf_delimit("+TESTI: 1"),
    )
}

// Extension response with one string argument
#[test]
fn one_string_arg() {
    test_roundtrips(
        highlevel::Response::Tests { field: String::from("abc") },
        lowlevel::Response::Success {
            name: String::from("TESTS"),
            is_extension: true,
            arguments: lowlevel::Arguments::ArgumentList(vec![
                lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::String(
                    String::from("abc"),
                )),
            ]),
        },
        cr_lf_delimit("+TESTS: abc"),
    )
}

// Extension response with one key-value argument
#[test]
fn one_kv_arg() {
    let mut map = HashMap::new();
    map.insert(1, String::from("abc"));

    test_roundtrips(
        highlevel::Response::Testm { field: map },
        lowlevel::Response::Success {
            name: String::from("TESTM"),
            is_extension: true,
            arguments: lowlevel::Arguments::ArgumentList(vec![
                lowlevel::Argument::KeyValueArgument {
                    key: lowlevel::PrimitiveArgument::Integer(1),
                    value: lowlevel::PrimitiveArgument::String(String::from("abc")),
                },
            ]),
        },
        cr_lf_delimit("+TESTM: 1=abc"),
    )
}

// Extension response with multiple arguments which form a list
#[test]
fn args_list() {
    test_roundtrips(
        highlevel::Response::Testl { field: vec![1, 2] },
        lowlevel::Response::Success {
            name: String::from("TESTL"),
            is_extension: true,
            arguments: lowlevel::Arguments::ArgumentList(vec![
                lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(1)),
                lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(2)),
            ]),
        },
        cr_lf_delimit("+TESTL: 1,2"),
    )
}

// Extension response with multiple arguments
#[test]
fn args() {
    test_roundtrips(
        highlevel::Response::Testsi { field1: String::from("abc"), field2: 1 },
        lowlevel::Response::Success {
            name: String::from("TESTSI"),
            is_extension: true,
            arguments: lowlevel::Arguments::ArgumentList(vec![
                lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::String(
                    String::from("abc"),
                )),
                lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(1)),
            ]),
        },
        cr_lf_delimit("+TESTSI: abc,1"),
    )
}

// Paren delimited argument list
#[test]
fn paren_args() {
    test_roundtrips(
        highlevel::Response::Testp { field: 1 },
        lowlevel::Response::Success {
            name: String::from("TESTP"),
            is_extension: true,
            arguments: lowlevel::Arguments::ParenthesisDelimitedArgumentLists(vec![vec![
                lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(1)),
            ]]),
        },
        cr_lf_delimit("+TESTP: (1)"),
    )
}

// Paren delimited multiple argument lists
#[test]
fn multiple_paren_args() {
    test_roundtrips(
        highlevel::Response::Testpp { field1: 1, field2: 2, field3: String::from("abc") },
        lowlevel::Response::Success {
            name: String::from("TESTPP"),
            is_extension: true,
            arguments: lowlevel::Arguments::ParenthesisDelimitedArgumentLists(vec![
                vec![lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(
                    1,
                ))],
                vec![
                    lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(2)),
                    lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::String(
                        String::from("abc"),
                    )),
                ],
            ]),
        },
        cr_lf_delimit("+TESTPP: (1)(2,abc)"),
    )
}

// Paren delimited multiple argument lists with map and list
#[test]
fn multiple_paren_kv_args() {
    let mut map = HashMap::new();
    map.insert(1, String::from("abc"));
    test_roundtrips(
        highlevel::Response::Testpmpil { field1: map, field2: 2, field3: vec![3, 4] },
        lowlevel::Response::Success {
            name: String::from("TESTPMPIL"),
            is_extension: true,
            arguments: lowlevel::Arguments::ParenthesisDelimitedArgumentLists(vec![
                vec![lowlevel::Argument::KeyValueArgument {
                    key: lowlevel::PrimitiveArgument::Integer(1),
                    value: lowlevel::PrimitiveArgument::String(String::from("abc")),
                }],
                vec![
                    lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(2)),
                    lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(3)),
                    lowlevel::Argument::PrimitiveArgument(lowlevel::PrimitiveArgument::Integer(4)),
                ],
            ]),
        },
        cr_lf_delimit("+TESTPMPIL: (1=abc)(2,3,4)"),
    )
}
