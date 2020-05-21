// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use proc_macro::TokenStream;
use proc_macro2::Span;
use quote::quote;

struct Variant {
    implementation: &'static str,
    suffix: &'static str,
}

fn variants_test(attr: TokenStream, input: TokenStream, variants: &[Variant]) -> TokenStream {
    if !attr.is_empty() {
        return syn::Error::new_spanned(
            proc_macro2::TokenStream::from(attr),
            "unrecognized attributes",
        )
        .to_compile_error()
        .into();
    }
    let item = syn::parse_macro_input!(input as syn::ItemFn);

    let name = &item.sig.ident;
    let output = &item.sig.output;
    if !item.sig.inputs.is_empty() {
        return syn::Error::new_spanned(&item.sig.inputs, "test function can't receive arguments")
            .to_compile_error()
            .into();
    }

    let defs = variants
        .iter()
        .map(|v| {
            let test_name =
                syn::Ident::new(&format!("{}_{}", name.to_string(), v.suffix), Span::call_site());
            let impl_ident = syn::Ident::new(v.implementation, Span::call_site());
            quote! {
                #[fuchsia_async::run_singlethreaded(test)]
                async fn #test_name () #output {
                    #name :: < #impl_ident >().await
                }
            }
        })
        .collect::<Vec<_>>();

    let result = quote! {
        #item
        #(#defs)*
    };

    result.into()
}

/// Runs a test `fn` over different variations of Netstacks.
///
/// Example:
///
/// ```
/// #[netstack_variants_test]
/// async fn test_foo<N: Netstack>() {}
/// ```
///
/// Expands to:
/// ```
/// async fn test_foo<N: Nestack>(){/*...*/}
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2() { test_foo::<Netstack2>().await }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3() { test_foo::<Netstack3>().await }
/// ```
#[proc_macro_attribute]
pub fn netstack_variants_test(attr: TokenStream, input: TokenStream) -> TokenStream {
    variants_test(
        attr,
        input,
        &[
            Variant { implementation: "Netstack2", suffix: "ns2" },
            Variant { implementation: "Netstack3", suffix: "ns3" },
        ],
    )
}

/// Runs a test `fn` over different variations of device endpoints.
///
/// Example:
///
/// ```
/// #[endpoint_variants_test]
/// async fn test_foo<E: Endpoint>() {}
/// ```
///
/// Expands equivalently to [`netstack_variants_test`].
#[proc_macro_attribute]
pub fn endpoint_variants_test(attr: TokenStream, input: TokenStream) -> TokenStream {
    variants_test(
        attr,
        input,
        &[
            Variant { implementation: "Ethernet", suffix: "eth" },
            Variant { implementation: "NetworkDevice", suffix: "netdevice" },
        ],
    )
}
