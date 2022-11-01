// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use quote::{quote, ToTokens as _};
use syn::{
    parse_quote, punctuated::Punctuated, spanned::Spanned, AngleBracketedGenericArguments,
    GenericArgument, GenericParam, Generics, Ident, Path, PathSegment, Type, TypeParam,
    TypeParamBound, TypePath,
};

/// Implements a derive macro for [`net_types::ip::GenericOverIp`].
#[proc_macro_derive(GenericOverIp)]
pub fn derive_generic_over_ip(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).unwrap();

    impl_derive_generic_over_ip(&ast).into()
}

fn impl_derive_generic_over_ip(ast: &syn::DeriveInput) -> TokenStream2 {
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

    // Drop the first and last tokens, which should be '<' and '>'
    let mut impl_generics = impl_generics.into_token_stream().into_iter().skip(1).peekable();
    let impl_generics = core::iter::from_fn(|| {
        impl_generics.next().and_then(|x| impl_generics.peek().is_some().then_some(x))
    })
    .collect::<TokenStream2>();

    match param {
        Some(to_replace) => {
            // Emit an impl that substitutes the identified type parameter
            // to produce the new GenericOverIp::Type.
            let generic_ip_name: Ident = parse_quote!(IpType);
            let IpGenericParam { ident, param_type, extra_bounds } = to_replace;
            let extra_bounds_target: Path = match param_type {
                IpGenericParamType::IpVersion => parse_quote!(#generic_ip_name),
                IpGenericParamType::IpAddress => parse_quote!(#generic_ip_name::Addr),
            };
            let generic_bounds =
                with_type_param_replaced(&ast.generics, ident, param_type, generic_ip_name.clone());

            quote! {
                impl <#impl_generics, #generic_ip_name: Ip>
                GenericOverIp<IpType> for #name #type_generics
                where #extra_bounds_target: #(#extra_bounds)+*, {
                    type Type = #name #generic_bounds;
                }
            }
        }
        None => {
            // The type is IP-invariant so `GenericOverIp::Type` is always Self.`
            quote! {
                impl <IpType: Ip, #impl_generics> GenericOverIp<IpType> for #name #type_generics {
                    type Type = Self;
                }
            }
        }
    }
}

#[derive(Debug)]
struct IpGenericParam<'i> {
    ident: &'i Ident,
    param_type: IpGenericParamType,
    extra_bounds: Vec<&'i TypeParamBound>,
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
            let extra_bounds = t.bounds.iter().filter(|b| b != &bound);
            if trait_bound.path.is_ident("Ip") {
                return Some(IpGenericParam {
                    ident: &t.ident,
                    param_type: IpGenericParamType::IpVersion,
                    extra_bounds: extra_bounds.collect(),
                });
            }
            if trait_bound.path.is_ident("IpAddress") {
                return Some(IpGenericParam {
                    ident: &t.ident,
                    param_type: IpGenericParamType::IpAddress,
                    extra_bounds: extra_bounds.collect(),
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
    to_find: &Ident,
    param_type: IpGenericParamType,
    ip_type_ident: Ident,
) -> Option<AngleBracketedGenericArguments> {
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
