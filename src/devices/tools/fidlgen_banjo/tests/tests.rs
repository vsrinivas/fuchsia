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
    macro_rules! c_test {
        ( $id:ident ) => {
            codegen_test!($id, CBackend, concat!("c/", stringify!($id), ".h"));
        };
    }

    c_test!(alias);
    c_test!(alignment);
    c_test!(api);
    c_test!(attributes);
    c_test!(binary);
    c_test!(buffer);
    c_test!(callback);
    c_test!(constants);
    c_test!(empty);
    c_test!(enums);
    c_test!(example0);
    c_test!(example1);
    c_test!(example2);
    c_test!(example3);
    c_test!(example4);
    c_test!(example6);
    c_test!(example7);
    c_test!(example8);
    c_test!(example9);
    c_test!(fidlhandle);
    c_test!(handles);
    c_test!(interface);
    c_test!(order);
    c_test!(order1);
    c_test!(order2);
    c_test!(order3);
    c_test!(order4);
    c_test!(passcallback);
    c_test!(point);
    c_test!(preservenames);
    c_test!(protocolarray);
    c_test!(protocolbase);
    c_test!(protocolhandle);
    c_test!(protocolothertypes);
    c_test!(protocolprimitive);
    c_test!(protocolvector);
    c_test!(references);
    c_test!(simple);
    c_test!(tables);
    c_test!(types);
    c_test!(union);
    c_test!(view);
}

mod rust {
    macro_rules! rust_test {
        ( $id:ident ) => {
            codegen_test!($id, RustBackend, concat!("rust/", stringify!($id), ".rs"));
        };
    }

    rust_test!(alignment);
    // rust_test!(attributes);  // Has union.
    rust_test!(empty);
    rust_test!(enums);
    rust_test!(example0);
    rust_test!(example1);
    rust_test!(example2);
    rust_test!(example3);
    rust_test!(example4);
    // rust_test!(example6);  // Has constant.
    rust_test!(example7);
    rust_test!(example8);
    // rust_test!(example9);  // Has constant.
    rust_test!(point);
    // rust_test!(rustderive);  // Has union.
    // rust_test!(simple);  // Has zx_status alias.
    rust_test!(tables);
    // rust_test!(types);  // Has union.
    rust_test!(view);
}
