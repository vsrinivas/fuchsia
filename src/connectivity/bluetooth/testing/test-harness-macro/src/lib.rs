// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate defines attribute proc macros for writing tests with its sibling test-harness crate.
//! Specifically, marking fns which take `test-harness::TestHarness` with the macros in this crate
//! enables them to run with the standard Rust test runner. While `run_singlethreaded_test` is
//! currently the only attribute available, further attributes could be defined for other execution
//! environments.

extern crate proc_macro;

use {
    proc_macro::TokenStream,
    quote::quote_spanned,
    syn::{parse_macro_input, Error, Visibility},
};

fn validate_item_fn(sig: &syn::Signature, vis: &syn::Visibility) -> Result<(), syn::Error> {
    // Disallow const, unsafe or abi linkage, generics etc
    if let Some(c) = &sig.constness {
        return Err(Error::new(c.span, "test-harness tests may not be 'const'"));
    }
    if let Some(u) = &sig.unsafety {
        return Err(Error::new(u.span, "test-harness tests may not be 'unsafe'"));
    }
    if let Some(abi) = &sig.abi {
        return Err(Error::new(
            abi.extern_token.span,
            "test-harness test may not have custom linkage",
        ));
    }
    if !sig.generics.params.is_empty() || sig.generics.where_clause.is_some() {
        return Err(Error::new(sig.fn_token.span, "test-harness tests may not have generics"));
    }
    if sig.inputs.len() != 1 {
        return Err(Error::new(
            sig.paren_token.span,
            "test-harness tests take exactly one argument, which must `impl TestHarness`",
        ));
    }
    if let Some(dot3) = &sig.variadic {
        return Err(Error::new(dot3.dots.spans[0], "test-harness tests may not be variadic"));
    }
    // Require the target function acknowledge it is async.
    if sig.asyncness.is_none() {
        return Err(Error::new(sig.ident.span(), "test-harness tests must be declared as 'async'"));
    }
    // The attributes defined in this crate purposefully mangle the names and visibility of the fns
    // to which they are applied. As such, they should not be applied to `pub` fns, which would
    // indicate that the client plans to use them elsewhere.
    if let Some(token_span) = match vis {
        Visibility::Public(pub_vis) => Some(pub_vis.pub_token.span),
        Visibility::Crate(crate_vis) => Some(crate_vis.crate_token.span),
        Visibility::Restricted(restricted_vis) => Some(restricted_vis.pub_token.span),
        Visibility::Inherited => None,
    } {
        return Err(Error::new(token_span, "test-harness tests cannot be called elsewhere, so they must have inherited (i.e. non-pub) visibility"));
    }
    Ok(())
}

/// Used to run tests that require TestHarness types as inputs on a singlethreaded executor. This
/// attribute should be used instead of `#[test]`, not in addition to it.
///
/// e.g.
///
///     ```
///     impl TestHarness for SomeHarness {..}
///
///     #[test_harness::run_singlethreaded_test]
///     async fn test_foo(harness: SomeHarness) -> Result<(),Error> {
///         // use harness
///     }
///     ```
#[proc_macro_attribute]
pub fn run_singlethreaded_test(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let item = parse_macro_input!(item as syn::ItemFn);
    let syn::ItemFn { attrs, sig, vis, block } = item;

    if let Err(e) = validate_item_fn(&sig, &vis) {
        return e.to_compile_error().into();
    }

    let ret_type = sig.output;
    let inputs = sig.inputs;
    let span = sig.ident.span();
    let ident = sig.ident;

    let output = quote_spanned! {span=>
        // Preserve any original attributes.
        #(#attrs)* #[test]
        fn #ident () #ret_type {
            async fn func(#inputs) #ret_type {
                #block
            }
            let func = move |_| { ::test_harness::run_with_harness(func) };
            ::test_harness::run_singlethreaded_test(func)
          }
    };
    output.into()
}
