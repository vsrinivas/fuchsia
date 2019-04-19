// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

mod ast_tests;
mod codegen_tests;
mod fidl_tests;

/// Asserts the left and right hand side are the same ignoring new line characters
#[macro_export]
macro_rules! assert_eq_trim {
    ($left:expr , $right:expr,) => {{
        let left_val_trim: String = $left.chars().filter(|c| *c != '\n').collect();
        let right_val_trim: String = $right.chars().filter(|c| *c != '\n').collect();
        assert_eq!(left_val_trim, right_val_trim);
    }};
    ($left:expr , $right:expr) => {{
        match (&($left), &($right)) {
            (left_val, right_val) => {
                let left_val_trim: String = left_val.chars().filter(|c| *c != '\n').collect();
                let right_val_trim: String = right_val.chars().filter(|c| *c != '\n').collect();
                assert_eq!(left_val_trim, right_val_trim);
            }
        }
    }};
    ($left:expr , $right:expr, $($arg:tt)*) => {{
        match (&($left), &($right)) {
            (left_val, right_val) => {
                if !(*left_val == *right_val) {
                    let left_val_trim: String = left_val.chars().filter(|c| *c != '\n').collect();
                    let right_val_trim: String = right_val.chars().filter(|c| *c != '\n').collect();
                    assert_eq!(left_val_trim, right_val_trim);
                }
            }
        }
    }};
}

/// Makes a banjo backend test.
/// Arguments:
///     id: name of test
///     backend: backend generator (CBackend, AstBackend, RustBackend, etc...)
///     [input files]: vector of path relative input files
///     output file: file to compare against generated output
#[macro_export]
macro_rules! codegen_test {
    ( $id:ident, $backend: ident, [ $( $banjo_file:expr),* ], $ast_file:expr ) => {
            #[test]
            fn $id() {
                use pest::Parser;
                use banjo_lib::parser::{Rule, BanjoParser};
                use banjo_lib::backends;
                let expected = include_str!($ast_file);
                let mut output = vec![];
                let mut pair_vec= Vec::new();
                {
                    $(
                        let input = include_str!($banjo_file);
                        pair_vec.push(BanjoParser::parse(Rule::file, input).unwrap());
                    )*
                };

                let ast = banjo_lib::ast::BanjoAst::parse(pair_vec, Vec::new()).unwrap();
                {
                    let mut backend: Box<dyn backends::Backend<_>> =
                        Box::new(backends::$backend::new(&mut output));
                    backend.codegen(ast).unwrap();
                }
                let output = String::from_utf8(output).unwrap();
                $crate::assert_eq_trim!(output, expected)
            }
    }
}
