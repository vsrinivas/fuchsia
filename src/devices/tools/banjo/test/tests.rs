// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

mod ast_tests;
mod codegen_tests;
mod fidl_tests;
mod negative_tests;

/// Makes a banjo backend test.
/// Arguments:
///     id: name of test
///     backend: backend generator (CBackend, AstBackend, RustBackend, etc...)
///     [input files]: vector of path relative input files
///     output file: file to compare against generated output
///     subtype: optional argument to backend generator
#[macro_export]
macro_rules! codegen_test {
    ( $id:ident, $backend: ident, [ $( $banjo_file:expr),* ], $ast_file:expr $(, $subtype:expr)? ) => {
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
                        Box::new(backends::$backend::new(&mut output $(, $subtype)?));
                    backend.codegen(ast).unwrap();
                }
                let output = String::from_utf8(output).unwrap();
                assert_eq!(output, expected);
            }
    }
}

/// Makes a negative banjo parse test.
/// Arguments:
///     id: name of test
///     [input files]: vector of path relative input files
#[macro_export]
macro_rules! negative_parse_test {
    ( $id:ident, $banjo_file:expr ) => {
        #[test]
        fn $id() {
            use banjo_lib::parser::{BanjoParser, Rule};
            use pest::Parser;
            let input = include_str!($banjo_file);
            assert_eq!(BanjoParser::parse(Rule::file, input).is_err(), true);
        }
    };
}
