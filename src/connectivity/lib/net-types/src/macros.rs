// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro::TokenStream;
use quote::quote;
use syn::{
    parse_quote, punctuated::Punctuated, spanned::Spanned, AngleBracketedGenericArguments,
    GenericArgument, GenericParam, Generics, Ident, Path, PathSegment, Type, TypeParam,
    TypeParamBound, TypePath,
};

/// Implements a derive macro for [`net_types::ip::GenericOverIp`].
#[proc_macro_derive(GenericOverIp)]
pub fn derive_generic_over_ip(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).unwrap();

    impl_derive_generic_over_ip(&ast)
}

fn impl_derive_generic_over_ip(ast: &syn::DeriveInput) -> TokenStream {
    let (impl_generics, type_generics, where_clause) = ast.generics.split_for_impl();
    if where_clause.is_some() {
        return quote! {
            compile_error!("deriving GenericOverIp for types with 'where' clauses is unsupported")
        }
        .into();
    }

    let name = &ast.ident;
    let param = match find_ip_generic_param(ast.generics.type_params()) {
        Ok(param) => param,
        Err(FindGenericsError::MultipleGenerics([a, b])) => {
            return syn::Error::new(
                ast.generics.span(),
                format!("found conflicting bounds: {:?} and {:?}", a, b),
            )
            .into_compile_error()
            .into()
        }
    };
    match param {
        Some(to_replace) => {
            // Emit an impl that substitutes the identified type parameter
            // to produce the new GenericOverIp::Type.
            let generic_ip_name: Ident = parse_quote!(IpType);
            let generic_bounds =
                with_type_param_replaced(&ast.generics, to_replace, generic_ip_name.clone());

            quote! {
                impl #impl_generics GenericOverIp for #name #type_generics {
                    type Type<#generic_ip_name: Ip> = #name #generic_bounds;
                }
            }
        }
        None => {
            // The type is IP-invariant so `GenericOverIp::Type` is always Self.`
            quote! {
                impl #impl_generics GenericOverIp for #name #type_generics {
                    type Type<IpType: Ip> = Self;
                }
            }
        }
    }
    .into()
}

#[derive(Debug)]
struct IpGenericParam<'i> {
    ident: &'i Ident,
    param_type: IpGenericParamType,
}

#[derive(Debug)]
enum IpGenericParamType {
    IpVersion,
    IpAddress,
}

enum FindGenericsError<'t> {
    MultipleGenerics([IpGenericParam<'t>; 2]),
}

/// Finds the type parameter that is generic over IP version or address.
fn find_ip_generic_param<'t>(
    generics: impl Iterator<Item = &'t TypeParam>,
) -> Result<Option<IpGenericParam<'t>>, FindGenericsError<'t>> {
    let mut found_params = generics.filter_map(|t| {
        for bound in &t.bounds {
            let trait_bound = match bound {
                TypeParamBound::Trait(t) => t,
                TypeParamBound::Lifetime(_) => continue,
            };
            if trait_bound.path.is_ident("Ip") {
                return Some(IpGenericParam {
                    ident: &t.ident,
                    param_type: IpGenericParamType::IpVersion,
                });
            }
            if trait_bound.path.is_ident("IpAddress") {
                return Some(IpGenericParam {
                    ident: &t.ident,
                    param_type: IpGenericParamType::IpAddress,
                });
            }
        }
        None
    });

    if let Some(found) = found_params.next() {
        // Make sure there aren't any other candidates.
        if let Some(other) = found_params.next() {
            return Err(FindGenericsError::MultipleGenerics([found, other]));
        }
        Ok(Some(found))
    } else {
        Ok(None)
    }
}

fn with_type_param_replaced(
    generics: &Generics,
    to_replace: IpGenericParam<'_>,
    ip_type_ident: Ident,
) -> Option<AngleBracketedGenericArguments> {
    let IpGenericParam { ident: to_find, param_type } = to_replace;
    let args: Punctuated<_, _> = generics
        .params
        .iter()
        .map(|g| match g {
            GenericParam::Const(c) => GenericArgument::Const(parse_quote!(#c.ident)),
            GenericParam::Lifetime(l) => GenericArgument::Lifetime(l.lifetime.clone()),
            GenericParam::Type(t) => {
                let path = if &t.ident == to_find {
                    match param_type {
                        IpGenericParamType::IpVersion => ip_type_ident.clone().into(),
                        IpGenericParamType::IpAddress => {
                            let segments =
                                [PathSegment::from(ip_type_ident.clone()), parse_quote!(Addr)]
                                    .into_iter()
                                    .collect();
                            Path { segments, leading_colon: None }
                        }
                    }
                } else {
                    t.ident.clone().into()
                };
                GenericArgument::Type(Type::Path(TypePath { path, qself: None }))
            }
        })
        .collect();
    (args.len() != 0).then(|| AngleBracketedGenericArguments {
        args,
        colon2_token: None,
        lt_token: Default::default(),
        gt_token: Default::default(),
    })
}
