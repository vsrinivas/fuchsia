// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::pseudo_directory_impl;

use {indoc::indoc, proc_macro2::TokenStream, std::str::FromStr};

// rustfmt is aligning all the strings, making them even harder to read.  TODO For some reason this
// does not seem to work, and I need explicit `#[rustfmt::skip]` on every test.
#[rustfmt::skip::macros(check_pseudo_directory_impl)]
// Rustfmt is messing up indentation inside the `assert!` macro.
#[rustfmt::skip]
macro_rules! check_pseudo_directory_impl {
    ($input:expr, $expected:expr) => {{
        let input = TokenStream::from_str($input).unwrap();
        let output = pseudo_directory_impl(input);
        let expected = $expected;
        assert!(
            output.to_string() == expected,
            "Generated code does not match the expected one.\n\
             Expected:\n\
             {}
             Actual:\n\
             {}
            ",
            expected,
            output
        );
    }};
}

#[test]
#[rustfmt::skip]
fn empty() {
    check_pseudo_directory_impl!(
        "",
        "{ \
             use :: fuchsia_vfs_pseudo_fs_mt :: directory :: entry_container :: DirectlyMutable ; \
             let __dir = :: fuchsia_vfs_pseudo_fs_mt :: directory :: immutable :: simple ( ) ; \
             __dir \
         }"
    );
}

#[test]
#[rustfmt::skip]
fn one_entry() {
    check_pseudo_directory_impl!(
        indoc!(
            r#"
            "name" => read_only_static("content"),
        "#
        ),
        "{ \
             use :: fuchsia_vfs_pseudo_fs_mt :: directory :: entry_container :: DirectlyMutable ; \
             let __dir = :: fuchsia_vfs_pseudo_fs_mt :: directory :: immutable :: simple ( ) ; \
             :: fuchsia_vfs_pseudo_fs_mt :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"name\" , \"Span\" , \
                 __dir . clone ( ) . add_entry ( \"name\" , read_only_static ( \"content\" ) ) ) ; \
             __dir \
        }"
    );
}

#[test]
#[rustfmt::skip]
fn two_entries() {
    check_pseudo_directory_impl!(
        indoc!(
            r#"
            "first" => read_only_static("A"),
            "second" => read_only_static("B"),
        "#
        ),
        "{ \
             use :: fuchsia_vfs_pseudo_fs_mt :: directory :: entry_container :: DirectlyMutable ; \
             let __dir = :: fuchsia_vfs_pseudo_fs_mt :: directory :: immutable :: simple ( ) ; \
             :: fuchsia_vfs_pseudo_fs_mt :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"first\" , \"Span\" , \
                 __dir . clone ( ) . add_entry ( \"first\" , read_only_static ( \"A\" ) ) ) ; \
             :: fuchsia_vfs_pseudo_fs_mt :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"second\" , \"Span\" , \
                 __dir . clone ( ) . add_entry ( \"second\" , read_only_static ( \"B\" ) ) ) ; \
             __dir \
         }"
    );
}

#[test]
#[rustfmt::skip]
fn assign_to() {
    check_pseudo_directory_impl!(
        indoc!(
            r#"
            my_dir ->
            "first" => read_only_static("A"),
            "second" => read_only_static("B"),
        "#
        ),
        "{ \
             use :: fuchsia_vfs_pseudo_fs_mt :: directory :: entry_container :: DirectlyMutable ; \
             my_dir = :: fuchsia_vfs_pseudo_fs_mt :: directory :: immutable :: simple ( ) ; \
             :: fuchsia_vfs_pseudo_fs_mt :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"first\" , \"Span\" , \
                 my_dir . clone ( ) . add_entry ( \"first\" , read_only_static ( \"A\" ) ) ) ; \
             :: fuchsia_vfs_pseudo_fs_mt :: pseudo_directory :: unwrap_add_entry_span ( \
                 \"second\" , \"Span\" , \
                 my_dir . clone ( ) . add_entry ( \"second\" , read_only_static ( \"B\" ) ) ) ; \
             my_dir . clone ( ) \
         }"
    );
}

#[test]
#[rustfmt::skip]
fn entry_has_name_from_ref() {
    check_pseudo_directory_impl!(
        indoc!(
            r#"
            test_name => read_only_static("content"),
        "#
        ),
        "{ \
             use :: fuchsia_vfs_pseudo_fs_mt :: directory :: entry_container :: DirectlyMutable ; \
             let __dir = :: fuchsia_vfs_pseudo_fs_mt :: directory :: immutable :: simple ( ) ; \
             :: fuchsia_vfs_pseudo_fs_mt :: pseudo_directory :: unwrap_add_entry_span ( \
                 test_name , \"Span\" , \
                 __dir . clone ( ) . add_entry ( test_name , read_only_static ( \"content\" ) ) ) ; \
             __dir \
         }"
    );
}
