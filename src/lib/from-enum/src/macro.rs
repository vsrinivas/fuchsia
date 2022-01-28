// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    darling::{ast, FromDeriveInput, FromField, FromVariant},
    proc_macro2::TokenStream,
    quote::quote,
    std::collections::HashSet,
    syn::{parse_macro_input, Ident, Type},
};

#[derive(FromField)]
struct EnumField {
    ty: Type,
}

#[derive(FromVariant)]
struct EnumVariant {
    ident: Ident,
    fields: ast::Fields<EnumField>,
}

#[derive(FromDeriveInput)]
#[darling(supports(enum_newtype))]
struct FromEnumOpts {
    ident: Ident,
    data: ast::Data<EnumVariant, ()>,
}

fn from_enum_derive_impl(input: syn::DeriveInput) -> TokenStream {
    let opts = match FromEnumOpts::from_derive_input(&input) {
        Ok(opts) => opts,
        Err(e) => return e.write_errors(),
    };
    let variants = match opts.data {
        ast::Data::Enum(variants) => variants,
        _ => unreachable!("guaranteed to be an enum"),
    };
    let enum_ident = &opts.ident;
    let mut types_seen = HashSet::new();
    let mut impls = Vec::new();
    for variant in variants {
        let variant_ident = &variant.ident;
        let field = &variant.fields.fields[0];
        let field_ty = &field.ty;
        if !types_seen.insert(field_ty.clone()) {
            return darling::Error::custom("no two variants can contain the same type")
                .with_span(&variant_ident)
                .write_errors();
        }
        impls.push(quote! {
            impl from_enum::FromEnum < #enum_ident > for #field_ty {
                fn from_enum(e: & #enum_ident) -> Option<&Self> {
                    match e {
                        #enum_ident :: #variant_ident (inner) => Some(inner),
                        _ => None,
                    }
                }
            }

            impl std::convert::From < #field_ty > for #enum_ident {
                fn from(f: #field_ty) -> Self {
                    Self :: #variant_ident (f)
                }
            }
        });
    }

    quote! {
        #(#impls)*
    }
}

#[proc_macro_derive(FromEnum)]
pub fn from_enum_derive(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    from_enum_derive_impl(parse_macro_input!(input)).into()
}
