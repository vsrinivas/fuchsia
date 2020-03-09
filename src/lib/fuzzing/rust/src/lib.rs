// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust/LibFuzzer integration for Fuchsia

extern crate proc_macro;

use {
    proc_macro::TokenStream,
    quote::{quote, quote_spanned},
    std::ops::Deref,
    syn::{
        parse::Error,
        parse_macro_input,
        FnArg::{self, Receiver, Typed},
        Type::Reference,
    },
};

/// Defines a fuzz target function.
///
/// This macro creates an exported function with a well-known symbol, `LLVMFuzzerTestOneInput`, as
/// defined in https://llvm.org/docs/LibFuzzer.html#fuzz-target. The macro can be used in two
/// ways:
///
/// The "manual" invocation simply takes a slice of bytes, uses them to exercise the API being
/// tested, and does not return anything. For example:
/// ```
/// use fuchsia_fuzzing::fuzz;
///
/// #[fuzz]
/// fn my_fuzzer(input: &[u8]) {
///     if let Some(x) = transform_bytes_to_something_else(input) {
///         do_something_with_my_api(x);
///     }
/// }
/// ```
///
/// The "automatic" invocation is more flexible: it can take one or more inputs of types with the
/// `Arbitrary` trait, use them to exercise the API being tested, and does not return anything.
/// For example:
/// ```
/// use {
///     arbitrary::Arbitrary,
///     fuchsia_fuzzing::fuzz,
/// };
///
/// #[derive(Arbitrary)]
/// pub enum Temp {
///     Celsius(i32),
///     Kelvin(u32),
/// }
///
/// #[derive(Arbitrary)]
/// pub struct Metrics {
///     name: Option<String>,
///     metrics: HashMap<String, Vec<Temp>>,
/// }
///
/// #[fuzz]
/// fn my_fuzzer(metrics: Metrics, log: Vec<String>) {
///    add_metrics_to_logs(metrics, logs);
/// }
/// ```
///
/// The recommended way to link the `LLVMFuzzerTestOneInput` into a fuzzer binary is to use the
/// `rustc_fuzzer` GN template from //build/rust/rustc_fuzzer.gni.
///
#[proc_macro_attribute]
pub fn fuzz(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let item = parse_macro_input!(item as syn::ItemFn);
    let syn::ItemFn { attrs, sig, vis: _, block } = item;
    let sig_ident = &sig.ident;
    let span = sig_ident.span();
    let name = sig_ident.to_string();

    // Validate function signature.
    if sig.inputs.len() == 0 {
        return Error::new(span, "expected at least 1 parameter").to_compile_error().into();
    }

    let first_input = sig.inputs.first().unwrap();
    if let Receiver(_r) = first_input {
        return Error::new(span, "fuzz target function cannot be a method")
            .to_compile_error()
            .into();
    }

    // Strip off a couple layers, and separate patterns and types.
    let num_inputs = sig.inputs.len();
    let mut input_pats = sig.inputs.iter().filter_map(|a| match a {
        Typed(p) => Some(p.pat.deref()),
        _ => None,
    });
    let mut input_tys = sig
        .inputs
        .iter()
        .filter_map(|a| match a {
            Typed(p) => Some(p.ty.deref()),
            _ => None,
        })
        .peekable();

    // Build an example of what an input to manual fuzz target function looks like.
    let manual_input: FnArg = syn::parse_quote! { input: &[u8] };
    let manual_input = match manual_input {
        Typed(p) => Some(p),
        _ => None,
    }
    .unwrap();

    // Detect which kind of fuzz target function we have, manual or automatic.
    let is_manual = if let (Reference(manual_ref), Reference(first_ref)) =
        (manual_input.ty.deref(), input_tys.peek().unwrap())
    {
        num_inputs == 1 && manual_ref.elem == first_ref.elem
    } else {
        false
    };

    let fuzz_target = if is_manual {
        // Manual fuzz target function: single byte slice input.
        let first_input_pat = input_pats.next();
        quote! {
            // Data must not be modified; make an immutable slice.
            let #first_input_pat = unsafe { std::slice::from_raw_parts(data, size) };
            let _ = #sig_ident(#first_input_pat);
        }
    } else {
        // Automatic fuzz target function: variable Arbitrary inputs.
        let input_tys_clone = input_tys.clone();
        let min_size = quote! { #(<#input_tys_clone as Arbitrary>::size_hint(0).0)+* };

        let input_pats_clone = input_pats.clone();
        let arbitrary_pats = quote! { #(Ok(#input_pats_clone)),* };

        let input_pats_clone = input_pats.clone();
        let invocation = quote! { #(#input_pats_clone),* };

        // It would be nice to use `quote`s interpolation repetition here, but we need to handle the
        // last input slightly differently than the others.
        let mut arbitrary_tys = quote! {};
        let input_tys_clone = input_tys.clone();
        for (i, input_ty) in input_tys_clone.enumerate() {
            if i != num_inputs - 1 {
                arbitrary_tys
                    .extend(quote! { <#input_ty as Arbitrary>::arbitrary(&mut unstructured), });
            } else {
                arbitrary_tys
                    .extend(quote! { <#input_ty as Arbitrary>::arbitrary_take_rest(unstructured) });
            }
        }
        quote! {
            use arbitrary::{Arbitrary, Unstructured};
            // Data must not be modified; make an immutable slice.
            let data = unsafe { std::slice::from_raw_parts(data, size) };
            // Early exit if not enough input bytes.
            if data.len() < #min_size {
                return 0;
            }
            let mut unstructured = Unstructured::new(data);
            if let ( #arbitrary_pats ) = ( #arbitrary_tys ) {
                let _ = #sig_ident(#invocation);
            }
        }
    };

    let output = quote_spanned! {span=>
        // This anonymous constant prevents Rust code from calling this symbol natively. It can
        // only be called by linking against the produced object file, e.g. with libFuzzer.
        const _: () = {
            // Creates a separate function to allow fuzzer authors to use `return`, etc.
            #[cfg(fuzz_target = #name)]
            #sig
            #block

            // This function wraps the fuzz target function above to create input parameters and
            // ensure the correct return value is used.
            #[cfg(fuzz_target = #name)]
            #[no_mangle]
            #(#attrs)*
            pub extern "C" fn LLVMFuzzerTestOneInput(data: *const u8, size: usize) -> i32 {
                #fuzz_target
                0 // Always return zero per  https://llvm.org/docs/LibFuzzer.html#fuzz-target.
            }
        };
    };
    output.into()
}
