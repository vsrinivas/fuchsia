// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    crate::common::{
        extract_expected, extract_min_length, extract_unique_items, gen_visit_seq, ident_from_path,
    },
    proc_macro2::TokenStream as TokenStream2,
    quote::{quote, ToTokens, TokenStreamExt},
    syn,
};

pub fn impl_derive_checked_vec(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let attrs = parse_checked_vec_attributes(&ast)?;

    struct Deserialize<'a> {
        ty: &'a syn::Path,
        inner_type: &'a syn::Path,
        expected: &'a syn::LitStr,
        min_length: Option<usize>,
        unique_items: bool,
    }
    impl ToTokens for Deserialize<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let ty = self.ty;
            let inner_type = self.inner_type;
            let expected = self.expected;
            let min_length = self.min_length;
            let unique_items = self.unique_items;
            let visit_seq =
                gen_visit_seq(quote!(#ty), inner_type, expected, min_length, unique_items);
            tokens.append_all(quote! {
                impl<'de> serde::de::Deserialize<'de> for #ty {
                    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
                    where
                        D: serde::de::Deserializer<'de>,
                    {
                        struct Visitor;
                        impl<'de> serde::de::Visitor<'de> for Visitor {
                            type Value = #ty;

                            fn expecting(
                                &self,
                                formatter: &mut fmt::Formatter<'_>
                            ) -> fmt::Result {
                                formatter.write_str(#expected)
                            }

                            #visit_seq
                        }
                        deserializer.deserialize_seq(Visitor)
                    }
                }
            });
        }
    }
    let deserialize = Deserialize {
        ty: &attrs.ty,
        inner_type: &attrs.inner_type,
        expected: &attrs.expected,
        min_length: attrs.min_length,
        unique_items: attrs.unique_items,
    };
    let tokens = quote! {
        #deserialize
    };
    Ok(tokens)
}

/// Attributes extracted from the `derive(CheckedVec)` macro.
struct CheckedVecAttributes {
    /// Type of the struct.
    ty: syn::Path,
    /// Type inside the Vec.
    inner_type: syn::Path,
    /// `expecting` string to return from the deserializer.
    expected: syn::LitStr,
    /// The minimum length of the vector, if any.
    min_length: Option<usize>,
    /// Whether all items in the vector must be unique.
    unique_items: bool,
}

fn parse_checked_vec_attributes(
    ast: &syn::DeriveInput,
) -> Result<CheckedVecAttributes, syn::Error> {
    let inner_type;
    match &ast.data {
        syn::Data::Struct(syn::DataStruct { fields: syn::Fields::Unnamed(fields), .. }) => {
            inner_type = get_vec_inner_type(&fields).map_err(|_| {
                syn::Error::new_spanned(
                    ast,
                    "CheckedVec must be derived on a struct with one unnamed Vec field",
                )
            })?;
        }
        _ => {
            return Err(syn::Error::new_spanned(
                ast,
                "CheckedVec must be derived on a struct with one unnamed Vec field",
            ));
        }
    }
    let mut expected = None;
    let mut min_length = None;
    let mut unique_items = None;
    for attr in &ast.attrs {
        if !attr.path.is_ident("checked_vec") {
            continue;
        }
        match attr
            .parse_meta()
            .map_err(|_| syn::Error::new_spanned(ast, "`checked_vec` attribute is not valid"))?
        {
            syn::Meta::List(l) => {
                for attr in l.nested {
                    match attr {
                        syn::NestedMeta::Meta(syn::Meta::NameValue(attr)) => {
                            let ident = ident_from_path(&attr.path);
                            match &ident as &str {
                                "expected" => extract_expected(ast, attr, &mut expected)?,
                                "min_length" => extract_min_length(ast, attr, &mut min_length)?,
                                "unique_items" => {
                                    extract_unique_items(ast, attr, &mut unique_items)?
                                }
                                _ => {
                                    return Err(syn::Error::new_spanned(
                                        ast,
                                        "`checked_vec` attribute is not valid",
                                    ));
                                }
                            }
                        }
                        _ => {
                            return Err(syn::Error::new_spanned(
                                ast,
                                "`checked_vec` attribute must contain name-value pairs",
                            ))?
                        }
                    }
                }
            }
            _ => {
                return Err(syn::Error::new_spanned(
                    ast,
                    "`checked_vec` attribute value must be a list",
                ));
            }
        }
    }

    let ty = ast.ident.clone().into();
    let expected =
        expected.ok_or_else(|| syn::Error::new_spanned(ast, "`expected` attribute is missing"))?;
    let unique_items = unique_items.unwrap_or(false);

    Ok(CheckedVecAttributes { ty, inner_type, expected, min_length, unique_items })
}

#[derive(Debug)]
enum ParseError {
    InvalidAttributes,
}

fn get_vec_inner_type(fields: &syn::FieldsUnnamed) -> Result<syn::Path, ParseError> {
    if fields.unnamed.len() != 1 {
        return Err(ParseError::InvalidAttributes);
    }
    let field = fields.unnamed.first().unwrap();
    match &field.ty {
        syn::Type::Path(ty) => {
            if ty.path.segments.len() != 1 {
                return Err(ParseError::InvalidAttributes);
            }
            let seg = &ty.path.segments.first().unwrap();
            if &seg.ident.to_string() != "Vec" {
                return Err(ParseError::InvalidAttributes);
            }
            match &seg.arguments {
                syn::PathArguments::AngleBracketed(a) => {
                    if a.args.len() != 1 {
                        return Err(ParseError::InvalidAttributes);
                    }
                    match a.args.first().unwrap() {
                        syn::GenericArgument::Type(syn::Type::Path(ty)) => {
                            return Ok(ty.path.clone());
                        }
                        _ => {}
                    }
                }
                _ => {}
            }
        }
        _ => {}
    }
    Err(ParseError::InvalidAttributes)
}
