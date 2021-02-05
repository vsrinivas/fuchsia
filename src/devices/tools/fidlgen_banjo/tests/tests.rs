// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

/// Makes a banjo backend test.
/// Arguments:
///     id: name of test
///     backend: backend generator (CBackend, etc...)
///     golden file: file to compare against generated output
#[macro_export]
macro_rules! codegen_test {
    ( $id:ident, $backend: ident, $golden_file:expr ) => {
        #[test]
        fn $id() -> Result<(), anyhow::Error> {
            use fidlgen_banjo_lib::{backends, fidl::FidlIr};
            use pretty_assertions::assert_eq;

            let ir: FidlIr = serde_json::from_str(test_irs::$id::IR)?;
            let mut output = vec![];
            let expected = include_str!($golden_file);

            {
                let mut backend: Box<dyn backends::Backend<'_, _>> =
                    Box::new(backends::$backend::new(&mut output));
                backend.codegen(ir).unwrap();
            }
            let output = String::from_utf8(output).unwrap();
            assert_eq!(output, expected);

            Ok(())
        }
    };
}

mod c {
    use super::codegen_test;

    codegen_test!(constants, CBackend, "c/constants.h");
    codegen_test!(enums, CBackend, "c/enums.h");
    codegen_test!(example0, CBackend, "c/example0.h");
    codegen_test!(example1, CBackend, "c/example1.h");
    codegen_test!(example2, CBackend, "c/example2.h");
    codegen_test!(example3, CBackend, "c/example3.h");
    codegen_test!(example4, CBackend, "c/example4.h");
    codegen_test!(example6, CBackend, "c/example6.h");
    codegen_test!(example7, CBackend, "c/example7.h");
    codegen_test!(example8, CBackend, "c/example8.h");
    codegen_test!(order, CBackend, "c/order.h");
}
