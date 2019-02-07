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

// TODO(surajmalhotra): Use macros to make these better.
const TEST_CASES: &'static [&'static [&'static str; 2]] = &[
    &[include_str!("alignment.test.banjo"), include_str!("alignment.test.ast")],
    &[include_str!("empty.test.banjo"), include_str!("empty.test.ast")],
    &[include_str!("enums.test.banjo"), include_str!("enums.test.ast")],
    // TODO(surajmalhotra): Requires nullable struct fields.
    //&[include_str!("example-0.test.banjo"), include_str!("example-0.test.ast")],
    &[include_str!("example-1.test.banjo"), include_str!("example-1.test.ast")],
    &[include_str!("example-2.test.banjo"), include_str!("example-2.test.ast")],
    &[include_str!("example-3.test.banjo"), include_str!("example-3.test.ast")],
    &[include_str!("example-4.test.banjo"), include_str!("example-4.test.ast")],
    &[include_str!("example-6.test.banjo"), include_str!("example-6.test.ast")],
    &[include_str!("example-7.test.banjo"), include_str!("example-7.test.ast")],
    &[include_str!("example-8.test.banjo"), include_str!("example-8.test.ast")],
    // TODO(surajmalhotra): Requires non-primitive const decls.
    //&[include_str!("example-9.test.banjo"), include_str!("example-9.test.ast")],
    &[include_str!("point.test.banjo"), include_str!("point.test.ast")],
    // TODO(bwb): Requires builtin zx library.
    //&[include_str!("simple.test.banjo"), include_str!("simple.test.ast")],
    &[include_str!("tables.test.banjo"), include_str!("tables.test.ast")],
    // TODO(bwb): Requires negative and floating point literals.
    //&[include_str!("types.test.banjo"), include_str!("types.test.ast")],
    // TODO(surajmalhotra): Depends on other libs.
    //&[include_str!("view.test.banjo"), include_str!("view.test.ast")],
];

#[test]
fn ast_backend() {
    for &case in TEST_CASES {
        let intput = case[0];
        let expected = case[1];
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
