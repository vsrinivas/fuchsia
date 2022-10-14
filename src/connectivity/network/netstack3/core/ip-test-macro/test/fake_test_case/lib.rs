// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A fake version of the `#[test_case]` macro to be used for testing the
//! `#[ip_test]` macro. This fake version appends the test case name as the
//! final argument to the test function.

use proc_macro::TokenStream;
use quote::quote_spanned;
use syn::{
    parse::{Parse, ParseStream},
    parse_macro_input,
    punctuated::Punctuated,
    Expr, Ident, ItemFn, LitStr, Token,
};

/// A #[test_case] macro that parses a comma-separated list of arguments,
/// followed by a semicolon, followed by a string literal. The arguments and
/// string literal are provided as arguments to the function to which this
/// attribute is attached.
#[proc_macro_attribute]
pub fn test_case(attr: TokenStream, item: TokenStream) -> TokenStream {
    let output = test_case_impl(attr, item);
    output
}

fn test_case_impl(attr: TokenStream, item: TokenStream) -> TokenStream {
    let ArgsAndTestName { args, name } = parse_macro_input!(attr as ArgsAndTestName);
    let item = parse_macro_input!(item as ItemFn);

    let fn_name = &item.sig.ident;
    let span = fn_name.span();

    let test_name = Ident::new(&format!("test_{}", fn_name), span);

    quote_spanned! { span =>
        #[test]
        fn #test_name() {
            #item

            #fn_name (#args, #name)
        }
    }
    .into()
}

/// A comma-separated list of arguments followed by a string literal.
struct ArgsAndTestName {
    args: Punctuated<Expr, Token![,]>,
    name: String,
}

impl Parse for ArgsAndTestName {
    fn parse(input: ParseStream<'_>) -> Result<Self, syn::Error> {
        Punctuated::parse_separated_nonempty(input).and_then(|args| {
            let _ = input.parse::<Token![;]>()?;
            let name = input.parse::<LitStr>()?.value();
            Ok(Self { args, name })
        })
    }
}
