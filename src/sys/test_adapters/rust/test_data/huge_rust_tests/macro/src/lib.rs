// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    proc_macro::TokenStream,
    proc_macro2::TokenStream as TokenStream2,
    quote::{format_ident, quote},
};

/// Generates many trivial tests.
#[proc_macro]
pub fn gen_tests(_item: TokenStream) -> TokenStream {
    let mut result = TokenStream2::new();
    for n in 1..=1_000 {
        let test_name = format_ident!("test_{}", n.to_string());
        result.extend(quote! {
            #[test]
            fn #test_name() {
                assert_eq!(#n, #n);
            }
        });
    }
    result.into()
}
