// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use proc_macro::TokenStream;
use proc_macro2::Span;
use quote::quote;

/// A specific implementation of a test variant.
struct Implementation {
    type_name: syn::Path,
    suffix: &'static str,
}

/// A variant tests will be generated for.
struct Variant<'a> {
    trait_bound: syn::Path,
    implementations: &'a [Implementation],
}

/// A specific variation of a test.
#[derive(Default)]
struct TestVariation {
    // Holds tuples of (trait name, implementation type name).
    trait_replacements: Vec<(syn::Path, syn::Path)>,
    suffix: String,
}

fn str_to_syn_path(path: &str) -> syn::Path {
    let mut segments = syn::punctuated::Punctuated::<_, syn::token::Colon2>::new();
    for seg in path.split("::") {
        segments.push(syn::PathSegment {
            ident: syn::Ident::new(seg, Span::call_site()),
            arguments: syn::PathArguments::None,
        });
    }
    syn::Path { leading_colon: None, segments }
}

fn variants_test_inner(input: TokenStream, variants: &[Variant<'_>]) -> TokenStream {
    let item = input.clone();
    let item = syn::parse_macro_input!(item as syn::ItemFn);
    let syn::Signature { ident: name, generics, output, inputs, .. } = &item.sig;
    if inputs.len() != 1 {
        return syn::Error::new_spanned(inputs, "test functions must have a name argument")
            .to_compile_error()
            .into();
    }

    // Should not panic because of the earlier check.
    let arg = inputs.last().expect("only expect a single argument for test functions");
    let arg_type = match arg {
        syn::FnArg::Typed(t) => &t.ty,
        other => {
            return syn::Error::new_spanned(
                inputs,
                format!(
                    "test functions may only have a typed argument (`&str`); got = {:#?}",
                    other
                ),
            )
            .to_compile_error()
            .into()
        }
    };
    let arg_type = match arg_type.as_ref() {
        syn::Type::Reference(r) => &r.elem,
        other => {
            return syn::Error::new_spanned(
                inputs,
                format!(
                    "test functions may only have a reference typed argument (`&str`); got = {:#?}",
                    other
                ),
            )
            .to_compile_error()
            .into()
        }
    };
    let arg_type = match arg_type.as_ref() {
        syn::Type::Path(p) => &p.path,
        other => {
            return syn::Error::new_spanned(
                inputs,
                format!("test functions may only have a reference to a typed argument (`&str`); got = {:#?}", other),
            )
            .to_compile_error()
            .into()
        }
    };
    if !arg_type.is_ident("str") {
        return syn::Error::new_spanned(inputs, "test functions may only have a `&str` argument")
            .to_compile_error()
            .into();
    }

    // We only care about generic type parameters and their last trait bound.
    let mut trait_bounds = Vec::with_capacity(generics.params.len());
    for gen in generics.params.iter() {
        let generic_type = match gen {
            syn::GenericParam::Type(t) => t,
            other => {
                return syn::Error::new_spanned(
                    proc_macro2::TokenStream::from(input),
                    format!("test functions only support generic parameters; got = {:#?}", other),
                )
                .to_compile_error()
                .into()
            }
        };

        if generic_type.bounds.len() != 1 {
            return syn::Error::new_spanned(
                proc_macro2::TokenStream::from(input),
                format!(
                    "test functions expect a single bound for each generic parameter; got = {:#?}",
                    generic_type.bounds
                ),
            )
            .to_compile_error()
            .into();
        }

        // Should not panic because of the earlier check.
        let type_bound = generic_type
            .bounds
            .last()
            .expect("only expect a single bound for each generic parameter");

        let trait_type_bound = match type_bound {
            syn::TypeParamBound::Trait(t) => &t.path,
            other => {
                return syn::Error::new_spanned(
                    proc_macro2::TokenStream::from(input),
                    format!(
                        "test functions only support trait type parameter bounds; got = {:#?}",
                        other
                    ),
                )
                .to_compile_error()
                .into()
            }
        };

        trait_bounds.push(trait_type_bound)
    }

    // Generate the list of test variations we will generate.
    //
    // The intial variation has no replacements or suffix.
    let test_variations = variants.into_iter().fold(vec![TestVariation::default()], |acc, v| {
        // If the test is not generic over `v`, then skip `v`.
        if !trait_bounds.iter().any(|trait_bound| *trait_bound == &v.trait_bound) {
            return acc;
        }

        acc.into_iter()
            .flat_map(|c| {
                v.implementations.iter().map(move |i| {
                    let mut tbs = c.trait_replacements.clone();
                    tbs.push((v.trait_bound.clone(), i.type_name.clone()));
                    TestVariation {
                        trait_replacements: tbs,
                        suffix: format!("{}_{}", c.suffix, i.suffix),
                    }
                })
            })
            .collect::<Vec<_>>()
    });

    let mut impls = Vec::with_capacity(test_variations.len());
    for v in test_variations.iter() {
        // We don't need to add an "_" betweeen the name and the suffix here as the suffix
        // will start with one.
        let test_name_str = format!("{}{}", name.to_string(), v.suffix);
        let test_name = syn::Ident::new(&test_name_str, Span::call_site());

        // Replace all the generics with concrete types.
        let mut params = Vec::with_capacity(trait_bounds.len());
        for trait_bound in trait_bounds.iter() {
            if let Some((_, tn)) = v.trait_replacements.iter().find(|(tb, _)| trait_bound == &tb) {
                params.push(tn);
            } else {
                return syn::Error::new_spanned(
                    proc_macro2::TokenStream::from(input),
                    format!("unexpected parameter bound = {:#?}", trait_bound),
                )
                .to_compile_error()
                .into();
            }
        }

        let mut args = syn::punctuated::Punctuated::<_, syn::token::Comma>::new();
        args.push(syn::Expr::Lit(syn::ExprLit {
            attrs: vec![],
            lit: syn::Lit::Str(syn::LitStr::new(&test_name_str, Span::call_site())),
        }));

        impls.push(quote! {
            #[fuchsia_async::run_singlethreaded(test)]
            async fn #test_name () #output {
                #name :: < #(#params),* > ( #args ).await
            }
        })
    }

    let result = quote! {
        #item
        #(#impls)*
    };

    result.into()
}

/// Runs a test `fn` over different variations of Netstacks, device endpoints and/or
/// network managers based on the test `fn`'s type parameters.
///
/// The test `fn` may only be generic over any combination of `Endpoint`, `Netstack`
/// and `Manager`. They may only have a single `&str` argumenent for the test variation's
/// name.
///
/// Example:
///
/// ```
/// #[variants_test]
/// async fn test_foo<N: Netstack>(name: &str) {}
/// ```
///
/// Expands to:
/// ```
/// async fn test_foo<N: Nestack>(name: &str){/*...*/}
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2() {
///     test_foo::<netstack_testing_common::environments::Netstack2>("test_foo_ns2").await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3() {
///     test_foo::<netstack_testing_common::environments::Netstack3>("test_foo_ns3").await
/// }
/// ```
///
/// Similarily,
/// ```
/// #[variants_test]
/// async fn test_foo<E: netemul::Endpoint>(name: &str) {/*...*/}
/// ```
///
/// and
///
/// ```
/// #[variants_test]
/// async fn test_foo<M: Manager>(name: &str) {/*...*/}
/// ```
///
/// Expands equivalently to the netstack variant.
///
/// This macro also supports expanding with multiple variations.
///
/// Example:
///
/// ```
/// #[variants_test]
/// async fn test_foo<N: Netstack, E: netemul::Endpoint>(name: &str) {/*...*/}
/// ```
///
/// Expands to:
/// ```
/// async fn test_foo<N: Nestack, E: netemul::Endpoint>(name: &str){/*...*/}
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2_eth() {
///     test_foo::<netstack_testing_common::environments::Netstack2, netemul::Ethernet>(
///         "test_foo_ns2_eth",
///     )
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3_eth() {
///     test_foo::<netstack_testing_common::environments::Netstack3, netemul::Ethernet>(
///         "test_foo_ns3_eth",
///     )
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2_netdev() {
///     test_foo::<
///         netstack_testing_common::environments::Netstack2,
///         netemul::NetworkDevice,
///     >("test_foo_ns2_netdev").await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3_netdev() {
///     test_foo::<
///         netstack_testing_common::environments::Netstack3,
///         netemul::NetworkDevice,
///     >("test_foo_ns3_netdev").await
/// }
/// ```
#[proc_macro_attribute]
pub fn variants_test(attrs: TokenStream, input: TokenStream) -> TokenStream {
    if !attrs.is_empty() {
        return syn::Error::new_spanned(
            proc_macro2::TokenStream::from(attrs),
            "unrecognized attributes",
        )
        .to_compile_error()
        .into();
    }

    variants_test_inner(
        input,
        &[
            Variant {
                trait_bound: str_to_syn_path("Netstack"),
                implementations: &[
                    Implementation {
                        type_name: str_to_syn_path(
                            "netstack_testing_common::environments::Netstack2",
                        ),
                        suffix: "ns2",
                    },
                    Implementation {
                        type_name: str_to_syn_path(
                            "netstack_testing_common::environments::Netstack3",
                        ),
                        suffix: "ns3",
                    },
                ],
            },
            Variant {
                trait_bound: str_to_syn_path("netemul::Endpoint"),
                implementations: &[
                    Implementation {
                        type_name: str_to_syn_path("netemul::Ethernet"),
                        suffix: "eth",
                    },
                    Implementation {
                        type_name: str_to_syn_path("netemul::NetworkDevice"),
                        suffix: "netdevice",
                    },
                ],
            },
            Variant {
                trait_bound: str_to_syn_path("Manager"),
                implementations: &[
                    Implementation {
                        type_name: str_to_syn_path("netstack_testing_common::environments::NetCfg"),
                        suffix: "netcfg",
                    },
                    Implementation {
                        type_name: str_to_syn_path(
                            "netstack_testing_common::environments::NetworkManager",
                        ),
                        suffix: "netmgr",
                    },
                ],
            },
        ],
    )
}
