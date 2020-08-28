// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::pseudo_directory_impl;

use {indoc::indoc, proc_macro2::TokenStream, std::str::FromStr};

fn check_pseudo_directory_impl(input: &str, expected_immutable: &str) {
    let input = TokenStream::from_str(input).unwrap();
    {
        let output = pseudo_directory_impl(false, input.clone());
        assert!(
            output.to_string() == expected_immutable,
            "Generated code for the immutable case does not match the expected one.\n\
             Expected:\n\
             {}
             Actual:\n\
             {}
            ",
            expected_immutable,
            output
        );
    }
    {
        let output = pseudo_directory_impl(true, input);
        let expected_mutable = expected_immutable.replace(" immutable ", " mutable ");
        assert!(
            output.to_string() == expected_mutable,
            "Generated code for the mutable case does not match the expected one.\n\
             Expected:\n\
             {}
             Actual:\n\
             {}
            ",
            expected_mutable,
            output
        );
    }
}

#[test]
// Rustfmt is messing up indentation of the manually formatted code.
#[rustfmt::skip]
fn empty() {
    check_pseudo_directory_impl(
        "",
        "{ \
             use :: vfs :: directory :: helper :: DirectlyMutable ; \
             let __dir = :: vfs :: directory :: immutable :: simple ( ) ; \
             __dir \
         }"
    );
}

#[test]
#[rustfmt::skip]
fn one_entry() {
    check_pseudo_directory_impl(
        indoc!(
            r#"
            "name" => read_only_static("content"),
        "#
        ),
        "{ \
             use :: vfs :: directory :: helper :: DirectlyMutable ; \
             let __dir = :: vfs :: directory :: immutable :: simple ( ) ; \
             :: vfs :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"name\" , \"bytes(1..7)\" , \
                 __dir . clone ( ) . add_entry ( \"name\" , read_only_static ( \"content\" ) ) ) ; \
             __dir \
        }"
    );
}

#[test]
#[rustfmt::skip]
fn two_entries() {
    check_pseudo_directory_impl(
        indoc!(
            r#"
            "first" => read_only_static("A"),
            "second" => read_only_static("B"),
        "#
        ),
        "{ \
             use :: vfs :: directory :: helper :: DirectlyMutable ; \
             let __dir = :: vfs :: directory :: immutable :: simple ( ) ; \
             :: vfs :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"first\" , \"bytes(1..8)\" , \
                 __dir . clone ( ) . add_entry ( \"first\" , read_only_static ( \"A\" ) ) ) ; \
             :: vfs :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"second\" , \"bytes(35..43)\" , \
                 __dir . clone ( ) . add_entry ( \"second\" , read_only_static ( \"B\" ) ) ) ; \
             __dir \
         }"
    );
}

#[test]
#[rustfmt::skip]
fn assign_to() {
    check_pseudo_directory_impl(
        indoc!(
            r#"
            my_dir ->
            "first" => read_only_static("A"),
            "second" => read_only_static("B"),
        "#
        ),
        "{ \
             use :: vfs :: directory :: helper :: DirectlyMutable ; \
             my_dir = :: vfs :: directory :: immutable :: simple ( ) ; \
             :: vfs :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"first\" , \"bytes(11..18)\" , \
                 my_dir . clone ( ) . add_entry ( \"first\" , read_only_static ( \"A\" ) ) ) ; \
             :: vfs :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"second\" , \"bytes(45..53)\" , \
                 my_dir . clone ( ) . add_entry ( \"second\" , read_only_static ( \"B\" ) ) ) ; \
             my_dir . clone ( ) \
         }"
    );
}

#[test]
#[rustfmt::skip]
fn entry_has_name_from_ref() {
    check_pseudo_directory_impl(
        indoc!(
            r#"
            test_name => read_only_static("content"),
        "#
        ),
        "{ \
             use :: vfs :: directory :: helper :: DirectlyMutable ; \
             let __dir = :: vfs :: directory :: immutable :: simple ( ) ; \
             :: vfs :: pseudo_directory :: unwrap_add_entry_span ( \
                 test_name , \"bytes(1..10)\" , \
                 __dir . clone ( ) . add_entry ( test_name , read_only_static ( \"content\" ) ) ) ; \
             __dir \
         }"
    );
}
