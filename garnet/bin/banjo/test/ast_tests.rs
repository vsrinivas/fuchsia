// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    banjo_lib::{
        ast,
        backends::{AstBackend, Backend},
        parser::{BanjoParser, Rule},
    },
    pest::Parser,
    pretty_assertions::assert_eq,
};

macro_rules! codegen_test {
    ( $id:ident, $banjo_file:expr, $ast_file:expr ) => {
            #[test]
            fn $id() {
                let intput = include_str!($banjo_file);
                let expected = include_str!($ast_file);
                let mut output = vec![];
                let mut pair_vec = Vec::new();
                pair_vec.push(BanjoParser::parse(Rule::file, intput).unwrap());

                let ast = ast::BanjoAst::parse(pair_vec).unwrap();
                {
                    let mut backend: Box<dyn Backend<_>> = Box::new(AstBackend::new(&mut output));
                    backend.codegen(ast).unwrap();
                }
                let output = String::from_utf8(output).unwrap();
                assert_eq!(output, expected)
            }
    }
}

codegen_test!(alignment, "alignment.test.banjo", "alignment.test.ast");
codegen_test!(empty, "empty.test.banjo", "empty.test.ast");
codegen_test!(enums, "enums.test.banjo", "enums.test.ast");
codegen_test!(example_0, "example-0.test.banjo", "example-0.test.ast");
codegen_test!(example_1, "example-1.test.banjo", "example-1.test.ast");
codegen_test!(example_2, "example-2.test.banjo", "example-2.test.ast");
codegen_test!(example_3, "example-3.test.banjo", "example-3.test.ast");
codegen_test!(example_4, "example-4.test.banjo", "example-4.test.ast");
codegen_test!(example_6, "example-6.test.banjo", "example-6.test.ast");
codegen_test!(example_7, "example-7.test.banjo", "example-7.test.ast");
codegen_test!(example_8, "example-8.test.banjo", "example-8.test.ast");
codegen_test!(example_9, "example-9.test.banjo", "example-9.test.ast");
codegen_test!(point, "point.test.banjo", "point.test.ast");
codegen_test!(table, "tables.test.banjo", "tables.test.ast");
