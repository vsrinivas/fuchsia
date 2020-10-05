// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Procedural macro crate for simplifying writing async entry points
//!
//! This crate should not be depended upon directly, but should be used through
//! the fuchsia_async crate, which re-exports all these symbols.
//!
//! Program entry points, by they `main` or `#[test]` functions, that want to
//! perform asynchronous actions typically involve writing a shim such as:
//!
//! ```
//! fn main() -> Result<(), Error> {
//!     let mut executor = fasync::Executor::new().unwrap();
//!     let actual_main = async {
//!         // Actual main code
//!         Ok(())
//!     };
//!     executor.run_singlethreaded(actual_main())
//! }
//! ```
//!
//! or
//!
//! ```
//! #[test]
//! fn test_foo() -> Result<(), Error> {
//!     let mut executor = fasync::Executor::new().unwrap();
//!     let test = async {
//!         // Actual test code here
//!         Ok(())
//!     };
//!     let mut test_fut = test();
//!     pin_mut!(test_fut);
//!     match executor.run_until_stalled(&mut test_fut) {
//!         Poll::Pending => panic!("Test blocked"),
//!         Poll::Ready(x) => x,
//!     }
//! }
//! ```
//!
//! This crate defines attributes that allow for writing the above as just
//!
//! ```
//! #[fuchsia_async::run_singlethreaded]
//! async fn main() -> Result<(), anyhow::Error> {
//!     // Actual main code
//!     Ok(())
//! }
//! ```
//!
//! or
//!
//! ```
//!
//! #[fuchsia_async::run_until_stalled(test)]
//! async fn test_foo() -> Result<(), Error> {
//!     // Actual test code here
//!     Ok(())
//! }
//! ```
//!
//! Using the optional 'test' specifier is preferred to using `#[test]`, as spurious
//! compilation errors will be generated should `#[test]` be specified before
//! `#[fuchsia_async::run_until_stalled]`.

extern crate proc_macro;

use {
    proc_macro::TokenStream,
    proc_macro2::Span,
    quote::{quote, quote_spanned},
    syn::{
        parse::{Error, Parse, ParseStream},
        parse_macro_input, Ident,
    },
};

mod kw {
    syn::custom_keyword!(test);
}

fn executor_ident() -> Ident {
    Ident::new("executor", Span::call_site())
}

fn common(item: TokenStream, run_executor: TokenStream, test: bool) -> TokenStream {
    let item = parse_macro_input!(item as syn::ItemFn);
    let syn::ItemFn { attrs, sig, vis: _, block } = item;
    if let Err(e) = (|| {
        // Disallow const, unsafe or abi linkage, generics etc
        if let Some(c) = &sig.constness {
            return Err(Error::new(c.span, "async entry may not be 'const'"));
        }
        if let Some(u) = &sig.unsafety {
            return Err(Error::new(u.span, "async entry may not be 'unsafe'"));
        }
        if let Some(abi) = &sig.abi {
            return Err(Error::new(
                abi.extern_token.span,
                "async entry may not have custom linkage",
            ));
        }
        if !sig.generics.params.is_empty() || sig.generics.where_clause.is_some() {
            return Err(Error::new(sig.fn_token.span, "async entry may not have generics"));
        }
        if !sig.inputs.is_empty() && !test {
            return Err(Error::new(sig.paren_token.span, "async entry takes no arguments"));
        }
        if let Some(dot3) = &sig.variadic {
            return Err(Error::new(dot3.dots.spans[0], "async entry may not be variadic"));
        }

        // Require the target function acknowledge it is async.
        if sig.asyncness.is_none() {
            return Err(Error::new(sig.ident.span(), "async entry must be declared as 'async'"));
        }

        // Only allow on 'main' or 'test' functions
        if sig.ident.to_string() != "main"
            && !test
            && !attrs.iter().any(|a| {
                a.parse_meta()
                    .map(|m| if let syn::Meta::Path(p) = m { p.is_ident("test") } else { false })
                    .unwrap_or(false)
            })
        {
            return Err(Error::new(sig.ident.span(), "async entry must a 'main' or '#[test]'."));
        }
        Ok(())
    })() {
        return e.to_compile_error().into();
    }

    let adapt_func = if test && sig.inputs.is_empty() {
        quote! {let func = move |_| func()}
    } else {
        quote! {}
    };
    let test = if test {
        quote! {#[test]}
    } else {
        quote! {}
    };
    let run_executor = proc_macro2::TokenStream::from(run_executor);
    let ret_type = sig.output;
    let inputs = sig.inputs;
    let span = sig.ident.span();
    let ident = sig.ident;
    let output = quote_spanned! {span=>
        // Preserve any original attributes.
        #(#attrs)* #test
        fn #ident () #ret_type {
            async fn func(#inputs) #ret_type {
                #block
            }
            #adapt_func;

            #run_executor
          }
    };
    output.into()
}

/// Define an `async` function that should complete without stalling.
///
/// If the async function should stall then a `panic` will be raised. For example:
///
/// ```
/// #[fuchsia_async::run_until_stalled]
/// async fn this_will_fail_and_not_block() -> Result<(), anyhow::Error> {
///     let () = future::empty().await;
///     Ok(())
/// }
/// ```
///
/// will cause an immediate panic instead of hanging.
///
/// This is mainly intended for testing, and takes an optional `test` argument.
///
/// ```
/// #[fuchsia_async::run_until_stalled(test)]
/// async fn test_foo() {}
/// ```
#[proc_macro_attribute]
pub fn run_until_stalled(attr: TokenStream, item: TokenStream) -> TokenStream {
    let test = parse_macro_input!(attr as Option<kw::test>).is_some();
    let executor = executor_ident();
    let run_executor = if test {
        quote! {
            let mut #executor = ::fuchsia_async::Executor::new()
                .expect("Failed to create executor");
            ::fuchsia_async::test_support::run_until_stalled_test(&mut #executor, func)
        }
    } else {
        quote! {
            let mut #executor = ::fuchsia_async::Executor::new()
                .expect("Failed to create executor");
            let mut fut = func();
            ::fuchsia_async::pin_mut!(fut);
            match #executor.run_until_stalled(&mut fut) {
                ::core::task::Poll::Ready(result) => result,
                _ => panic!("Stalled without completing. Consider using \"run_singlethreaded\", \
                             or check for a deadlock."),
            }
        }
    };
    common(item, run_executor.into(), test)
}

/// Define an `async` function that should run to completion on a single thread.
///
/// Takes an optional `test` argument.
///
/// Tests written using this macro can be run repeatedly.
/// The environment variable `FASYNC_TEST_REPEAT_COUNT` sets the number of repetitions each test
/// will be run for, while the environment variable `FASYNC_TEST_MAX_CONCURRENCY` bounds the
/// maximum concurrent invocations of each test. If `FASYNC_TEST_MAX_CONCURRENCY` is set to 0 (the
/// default) no bounds to concurrency are applied.
/// If FASYNC_TEST_TIMEOUT_SECONDS is set, it specifies the maximum duration for one repetition of
/// a test.
///
/// ```
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo() {}
/// ```
///
/// The test can optionally take a usize argument which specifies which repetition of the test is
/// being performed:
///
/// ```
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo(test_run: usize) {
///   println!("Test repetition #{}", test_run);
/// }
/// ```
#[proc_macro_attribute]
pub fn run_singlethreaded(attr: TokenStream, item: TokenStream) -> TokenStream {
    let test = parse_macro_input!(attr as Option<kw::test>).is_some();
    let run_executor = if test {
        quote! {
            ::fuchsia_async::test_support::run_singlethreaded_test(func)
        }
    } else {
        quote! {
            ::fuchsia_async::Executor::new()
                .expect("Failed to create executor")
                .run_singlethreaded(func())
        }
    };
    common(item, run_executor.into(), test)
}

struct RunAttributes {
    threads: usize,
    test: bool,
}

impl Parse for RunAttributes {
    fn parse(input: ParseStream<'_>) -> syn::parse::Result<Self> {
        let threads = input.parse::<syn::LitInt>()?.base10_parse::<usize>()?;
        let comma = input.parse::<Option<syn::Token![,]>>()?.is_some();
        let test = if comma { input.parse::<Option<kw::test>>()?.is_some() } else { false };
        Ok(RunAttributes { threads, test })
    }
}

/// Define an `async` function that should run to completion on `N` threads.
///
/// Number of threads is configured by `#[fuchsia_async::run(N)]`, and can also
/// take an optional `test` argument.
///
/// Tests written using this macro can be run repeatedly.
/// The environment variable `FASYNC_TEST_REPEAT_COUNT` sets the number of repetitions each test
/// will be run for, while the environment variable `FASYNC_TEST_MAX_CONCURRENCY` bounds the
/// maximum concurrent invocations of each test. If `FASYNC_TEST_MAX_CONCURRENCY` is set to 0 (the
/// default) no bounds to concurrency are applied.
/// If FASYNC_TEST_TIMEOUT_SECONDS is set, it specifies the maximum duration for one repetition of
/// a test.
///
/// ```
/// #[fuchsia_async::run(4, test)]
/// async fn test_foo() {}
/// ```
///
/// The test can optionally take a usize argument which specifies which repetition of the test is
/// being performed:
///
/// ```
/// #[fuchsia_async::run(4, test)]
/// async fn test_foo(test_run: usize) {
///   println!("Test repetition #{}", test_run);
/// }
/// ```
#[proc_macro_attribute]
pub fn run(attr: TokenStream, item: TokenStream) -> TokenStream {
    let RunAttributes { threads, test } = parse_macro_input!(attr as RunAttributes);

    let run_executor = if test {
        quote! {
            ::fuchsia_async::test_support::run_test(func, #threads)
        }
    } else {
        quote! {
            ::fuchsia_async::Executor::new()
                .expect("Failed to create executor")
                .run(func(), #threads)
        }
    };
    common(item, run_executor.into(), test)
}
