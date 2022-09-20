// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#[derive(Default, Debug)]
struct TestVariation {
    /// Params that we use to instantiate the test.
    params: Vec<syn::Path>,
    /// Unrelated bounds that we pass along.
    generics: Vec<syn::TypeParam>,
    /// Suffix of the test name.
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

fn permutations_over_type_generics(
    variants: &[Variant<'_>],
    type_generics: &[&syn::TypeParam],
) -> Vec<TestVariation> {
    // Generate the permutations by substituting implementations for generics
    // with a recursive depth-first search.
    fn do_permutation(
        variants: &[Variant<'_>],
        type_generics: &[&syn::TypeParam],
        suffix: String,
        params: &mut Vec<syn::Path>,
        generics: &mut Vec<syn::TypeParam>,
        test_variations: &mut Vec<TestVariation>,
    ) {
        let (first_generic, rest_generics) = match type_generics.split_first() {
            Some(split) => split,
            None => {
                test_variations.push(TestVariation {
                    params: params.clone(),
                    generics: generics.clone(),
                    suffix,
                });
                return;
            }
        };

        let variants_for_first_generic: Option<&Variant<'_>> =
            first_generic.bounds.iter().find_map(|b| {
                let t = match b {
                    syn::TypeParamBound::Trait(t) => t,
                    syn::TypeParamBound::Lifetime(_) => return None,
                };
                variants.iter().find(|v| v.trait_bound == t.path)
            });
        match variants_for_first_generic {
            None => {
                // This parameter is not related to a test variation, keep the
                // parameter in the generated function.
                params.push(syn::Path::from(syn::PathSegment::from(first_generic.ident.clone())));
                generics.push((*first_generic).clone());
                do_permutation(variants, rest_generics, suffix, params, generics, test_variations);
                let _: Option<_> = params.pop();
                let _: Option<_> = generics.pop();
            }
            Some(Variant { trait_bound: _, implementations }) => {
                for Implementation { type_name, suffix: s } in *implementations {
                    params.push(type_name.clone());
                    do_permutation(
                        variants,
                        rest_generics,
                        format!("{}_{}", suffix, *s),
                        params,
                        generics,
                        test_variations,
                    );
                    let _: Option<_> = params.pop();
                }
            }
        }
    }

    let mut test_variations = Vec::new();
    do_permutation(
        variants,
        &type_generics,
        String::new(),
        &mut Vec::new(),
        &mut Vec::new(),
        &mut test_variations,
    );
    test_variations
}

fn variants_test_inner(input: TokenStream, variants: &[Variant<'_>]) -> TokenStream {
    let item = input.clone();
    let mut item = syn::parse_macro_input!(item as syn::ItemFn);
    let syn::ItemFn { attrs, vis: _, ref sig, block: _ } = &mut item;
    let impl_attrs = std::mem::replace(attrs, Vec::new());
    let syn::Signature {
        constness: _,
        asyncness: _,
        unsafety: _,
        abi: _,
        fn_token: _,
        ident: name,
        generics: syn::Generics { lt_token: _, params, gt_token: _, where_clause },
        paren_token: _,
        inputs,
        variadic: _,
        output,
    } = sig;

    let arg = if let Some(arg) = inputs.first() {
        arg
    } else {
        return syn::Error::new_spanned(inputs, "test functions must have a name argument")
            .to_compile_error()
            .into();
    };

    let arg_type = match arg {
        syn::FnArg::Typed(syn::PatType { attrs: _, pat: _, colon_token: _, ty }) => ty,
        other => {
            return syn::Error::new_spanned(
                inputs,
                format!(
                    "test function's first argument must be a `&str` for test name; got = {:#?}",
                    other
                ),
            )
            .to_compile_error()
            .into()
        }
    };

    let arg_type = match arg_type.as_ref() {
        syn::Type::Reference(syn::TypeReference {
            and_token: _,
            lifetime: _,
            mutability: _,
            elem,
        }) => elem,
        other => {
            return syn::Error::new_spanned(
                inputs,
                format!(
                    "test function's first argument must be a `&str` for test name; got = {:#?}",
                    other
                ),
            )
            .to_compile_error()
            .into()
        }
    };

    let arg_type = match arg_type.as_ref() {
        syn::Type::Path(syn::TypePath { qself: _, path }) => path,
        other => {
            return syn::Error::new_spanned(
                inputs,
                format!(
                    "test function's first argument must be a `&str` for test name; got = {:#?}",
                    other
                ),
            )
            .to_compile_error()
            .into()
        }
    };

    if !arg_type.is_ident("str") {
        return syn::Error::new_spanned(
            inputs,
            "test function's first argument must be a `&str`  for test name",
        )
        .to_compile_error()
        .into();
    }

    // We only care about generic type parameters and their last trait bound.
    let mut type_generics = Vec::with_capacity(params.len());
    for gen in params.iter() {
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

        type_generics.push(generic_type)
    }

    let mut impls = Vec::new();
    // Generate the list of test variations we will generate.
    //
    // The initial variation has no replacements or suffix.
    for TestVariation { params, generics, suffix } in
        permutations_over_type_generics(variants, &type_generics)
    {
        // We don't need to add an "_" between the name and the suffix here as the suffix
        // will start with one.
        let test_name_str = format!("{}{}", name.to_string(), suffix);
        let test_name = syn::Ident::new(&test_name_str, Span::call_site());

        // Pass the test name as the first argument, and keep other arguments
        // in the generated function which will be passed to the original function.
        let impl_inputs = inputs
            .iter()
            .enumerate()
            .filter_map(|(i, item)| if i == 0 { None } else { Some(item.clone()) })
            .collect::<Vec<_>>();
        let mut args = vec![syn::Expr::Lit(syn::ExprLit {
            attrs: vec![],
            lit: syn::Lit::Str(syn::LitStr::new(&test_name_str, Span::call_site())),
        })];
        for arg in impl_inputs.iter() {
            let arg = match arg {
                syn::FnArg::Typed(syn::PatType { attrs: _, pat, colon_token: _, ty: _ }) => pat,
                other => {
                    return syn::Error::new_spanned(
                        proc_macro2::TokenStream::from(input),
                        format!("expected typed fn arg; got = {:#?}", other),
                    )
                    .to_compile_error()
                    .into()
                }
            };

            let arg = match arg.as_ref() {
                syn::Pat::Ident(syn::PatIdent {
                    attrs: _,
                    by_ref: _,
                    mutability: _,
                    ident,
                    subpat: _,
                }) => ident,
                other => {
                    return syn::Error::new_spanned(
                        proc_macro2::TokenStream::from(input),
                        format!("expected ident fn arg; got = {:#?}", other),
                    )
                    .to_compile_error()
                    .into()
                }
            };

            args.push(syn::Expr::Path(syn::ExprPath {
                attrs: Vec::new(),
                qself: None,
                path: arg.clone().into(),
            }));
        }

        impls.push(quote! {
            #(#impl_attrs)*
            #[fuchsia_async::run_singlethreaded(test)]
            async fn #test_name < #(#generics),* > ( #(#impl_inputs),* ) #output #where_clause {
                #name :: < #(#params),* > ( #(#args),* ).await
            }
        });
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
/// and `Manager`. It may only have a single `&str` argument, used to identify the
/// test variation.
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
/// async fn test_foo<N: Netstack>(name: &str){/*...*/}
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2() {
///     test_foo::<netstack_testing_common::realms::Netstack2>("test_foo_ns2").await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3() {
///     test_foo::<netstack_testing_common::realms::Netstack3>("test_foo_ns3").await
/// }
/// ```
///
/// Similarly,
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
/// async fn test_foo<N: Netstack, E: netemul::Endpoint>(name: &str){/*...*/}
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2_eth() {
///     test_foo::<netstack_testing_common::realms::Netstack2, netemul::Ethernet>(
///         "test_foo_ns2_eth",
///     )
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3_eth() {
///     test_foo::<netstack_testing_common::realms::Netstack3, netemul::Ethernet>(
///         "test_foo_ns3_eth",
///     )
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2_netdevice() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack2,
///         netemul::NetworkDevice,
///     >("test_foo_ns2_netdevice").await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3_netdevice() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack3,
///         netemul::NetworkDevice,
///     >("test_foo_ns3_netdevice").await
/// }
/// ```
//
/// Similarly, this macro also handles expanding multiple occurrences of the
/// same trait bound.
/// ```
/// #[variants_test]
/// async fn test_foo<N1: Netstack, N2: Netstack, E: netemul::Endpoint>(name: &str) {/*...*/}
/// ```
///
/// Expands to:
/// ```
/// async fn test_foo<N1: Netstack, N2: Netstack, E: netemul::Endpoint>(name: &str) {/*...*/}
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2_ns2_eth() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack2,
///         netstack_testing_common::realms::Netstack2,
///         netemul::Ethernet,
///     >("test_foo_ns2_ns2_eth")
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2_ns2_netdevice() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack2,
///         netstack_testing_common::realms::Netstack2,
///         netemul::NetworkDevice,
///     >("test_foo_ns2_ns2_netdevice")
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2_ns3_eth() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack2,
///         netstack_testing_common::realms::Netstack3,
///         netemul::Ethernet,
///     >("test_foo_ns2_ns3_eth")
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns2_ns3_netdevice() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack2,
///         netstack_testing_common::realms::Netstack3,
///         netemul::NetworkDevice,
///     >("test_foo_ns2_ns3_netdevice")
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3_ns2_eth() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack3,
///         netstack_testing_common::realms::Netstack2,
///         netemul::Ethernet,
///     >("test_foo_ns3_ns2_eth")
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3_ns2_netdevice() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack3,
///         netstack_testing_common::realms::Netstack2,
///         netemul::NetworkDevice,
///     >("test_foo_ns3_ns2_netdevice")
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3_ns3_eth() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack3,
///         netstack_testing_common::realms::Netstack3,
///         netemul::Ethernet,
///     >("test_foo_ns3_ns3_eth")
///     .await
/// }
/// #[fuchsia_async::run_singlethreaded(test)]
/// async fn test_foo_ns3_ns3_netdevice() {
///     test_foo::<
///         netstack_testing_common::realms::Netstack3,
///         netstack_testing_common::realms::Netstack3,
///         netemul::NetworkDevice,
///     >("test_foo_ns3_ns3_netdevice")
///     .await
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
                        type_name: str_to_syn_path("netstack_testing_common::realms::Netstack2"),
                        suffix: "ns2",
                    },
                    Implementation {
                        type_name: str_to_syn_path("netstack_testing_common::realms::Netstack3"),
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
                        type_name: str_to_syn_path("netstack_testing_common::realms::NetCfgBasic"),
                        suffix: "netcfg_basic",
                    },
                    Implementation {
                        type_name: str_to_syn_path(
                            "netstack_testing_common::realms::NetCfgAdvanced",
                        ),
                        suffix: "netcfg_advanced",
                    },
                ],
            },
            Variant {
                trait_bound: str_to_syn_path("net_types::ip::Ip"),
                implementations: &[
                    Implementation {
                        type_name: str_to_syn_path("net_types::ip::Ipv6"),
                        suffix: "v6",
                    },
                    Implementation {
                        type_name: str_to_syn_path("net_types::ip::Ipv4"),
                        suffix: "v4",
                    },
                ],
            },
        ],
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use test_case::test_case;

    #[derive(Debug)]
    struct VariantExpectation {
        params: Vec<&'static str>,
        suffix: &'static str,
    }

    impl PartialEq<TestVariation> for VariantExpectation {
        fn eq(&self, other: &TestVariation) -> bool {
            self.suffix == other.suffix
                && self.params
                    == other
                        .params
                        .iter()
                        .map(|p| p.get_ident().unwrap().to_string())
                        .collect::<Vec<_>>()
        }
    }

    #[test_case(vec![] => vec![VariantExpectation {
        params: vec![],
        suffix: "",
    }]; "default")]
    #[test_case(vec!["T: TraitA"] => vec![VariantExpectation {
        params: vec!["ImplA1"],
        suffix: "_a1",
    }, VariantExpectation {
        params: vec!["ImplA2"],
        suffix: "_a2",
    }]; "simple case")]
    #[test_case(vec!["T: TraitA", "S: TraitB"] => vec![VariantExpectation {
        params: vec!["ImplA1", "ImplB1"],
        suffix: "_a1_b1",
    }, VariantExpectation {
        params: vec!["ImplA1", "ImplB2"],
        suffix: "_a1_b2",
    }, VariantExpectation {
        params: vec!["ImplA2", "ImplB1"],
        suffix: "_a2_b1",
    }, VariantExpectation {
        params: vec!["ImplA2", "ImplB2"],
        suffix: "_a2_b2",
    }]; "two traits")]
    #[test_case(vec!["T1: TraitA", "T2: TraitA"] => vec![VariantExpectation {
        params: vec!["ImplA1", "ImplA1"],
        suffix: "_a1_a1",
    }, VariantExpectation {
        params: vec!["ImplA1", "ImplA2"],
        suffix: "_a1_a2",
    }, VariantExpectation {
        params: vec!["ImplA2", "ImplA1"],
        suffix: "_a2_a1",
    }, VariantExpectation {
        params: vec!["ImplA2", "ImplA2"],
        suffix: "_a2_a2",
    }]; "two occurrences of a single trait")]
    fn permutation(generics: impl IntoIterator<Item = &'static str>) -> Vec<TestVariation> {
        let generics = generics
            .into_iter()
            .map(|g| syn::parse_str(g).unwrap())
            .collect::<Vec<syn::TypeParam>>();
        let generics = generics.iter().collect::<Vec<&_>>();

        permutations_over_type_generics(
            &[
                Variant {
                    trait_bound: syn::parse_str("TraitA").unwrap(),
                    implementations: &[
                        Implementation {
                            type_name: syn::parse_str("ImplA1").unwrap(),
                            suffix: "a1",
                        },
                        Implementation {
                            type_name: syn::parse_str("ImplA2").unwrap(),
                            suffix: "a2",
                        },
                    ],
                },
                Variant {
                    trait_bound: syn::parse_str("TraitB").unwrap(),
                    implementations: &[
                        Implementation {
                            type_name: syn::parse_str("ImplB1").unwrap(),
                            suffix: "b1",
                        },
                        Implementation {
                            type_name: syn::parse_str("ImplB2").unwrap(),
                            suffix: "b2",
                        },
                    ],
                },
            ],
            &generics,
        )
    }
}
