// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    crate::common::{
        extract_expected, extract_inner_type, extract_min_length, extract_unique_items,
        gen_visit_seq, gen_visit_str, ident_from_path,
    },
    proc_macro2::TokenStream as TokenStream2,
    quote::{quote, ToTokens, TokenStreamExt},
    syn,
};

pub fn impl_derive_one_or_many(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let attrs = parse_one_or_many_attributes(&ast)?;

    struct Deserialize<'a> {
        inner_type: &'a syn::Path,
        expected: &'a syn::LitStr,
        min_length: Option<usize>,
        unique_items: bool,
    }
    impl ToTokens for Deserialize<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let inner_type = self.inner_type;
            let expected = self.expected;
            let min_length = self.min_length;
            let unique_items = self.unique_items;
            let visit_str = gen_visit_str(Some(quote!(OneOrMany::One)), expected);
            let visit_seq = gen_visit_seq(
                quote!(OneOrMany::Many),
                inner_type,
                expected,
                min_length,
                unique_items,
            );
            tokens.append_all(quote! {
                impl<'de> serde::de::Deserialize<'de> for OneOrMany<#inner_type> {
                    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
                    where
                        D: serde::de::Deserializer<'de>,
                    {
                        struct Visitor;
                        impl<'de> serde::de::Visitor<'de> for Visitor {
                            type Value = OneOrMany<#inner_type>;

                            fn expecting(
                                &self,
                                formatter: &mut fmt::Formatter<'_>
                            ) -> fmt::Result {
                                formatter.write_str(#expected)
                            }

                            #visit_str

                            #visit_seq
                        }
                        deserializer.deserialize_any(Visitor)
                    }
                }
            });
        }
    }
    let deserialize = Deserialize {
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

/// Attributes extracted from the `derive(OneOrMany)` macro.
struct OneOrManyAttributes {
    /// Type inside the `OneOrMany`.
    inner_type: syn::Path,
    /// `expecting` string to return from the deserializer.
    expected: syn::LitStr,
    /// The minimum length of the vector, if any.
    min_length: Option<usize>,
    /// Whether all items in the vector must be unique.
    unique_items: bool,
}

fn parse_one_or_many_attributes(ast: &syn::DeriveInput) -> Result<OneOrManyAttributes, syn::Error> {
    match ast.data {
        syn::Data::Struct(syn::DataStruct { fields: syn::Fields::Unit, .. }) => {}
        _ => {
            return Err(syn::Error::new_spanned(
                ast,
                "OneOrMany must be derived on a struct with no fields",
            ));
        }
    }
    let mut expected = None;
    let mut inner_type = None;
    let mut min_length = None;
    let mut unique_items = None;
    for attr in &ast.attrs {
        if !attr.path.is_ident("one_or_many") {
            continue;
        }
        match attr
            .parse_meta()
            .map_err(|_| syn::Error::new_spanned(ast, "`one_or_many` attribute is not valid"))?
        {
            syn::Meta::List(l) => {
                for attr in l.nested {
                    match attr {
                        syn::NestedMeta::Meta(syn::Meta::NameValue(attr)) => {
                            let ident = ident_from_path(&attr.path);
                            match &ident as &str {
                                "expected" => extract_expected(ast, attr, &mut expected)?,
                                "inner_type" => extract_inner_type(ast, attr, &mut inner_type)?,
                                "min_length" => extract_min_length(ast, attr, &mut min_length)?,
                                "unique_items" => {
                                    extract_unique_items(ast, attr, &mut unique_items)?
                                }
                                _ => {
                                    return Err(syn::Error::new_spanned(
                                        ast,
                                        "`one_or_many` attribute is not valid",
                                    ));
                                }
                            }
                        }
                        _ => {
                            return Err(syn::Error::new_spanned(
                                ast,
                                "`one_or_many` attribute must contain name-value pairs",
                            ))?
                        }
                    }
                }
            }
            _ => {
                return Err(syn::Error::new_spanned(
                    ast,
                    "`one_or_many` attribute value must be a list",
                ));
            }
        }
    }

    let inner_type: syn::Path = inner_type
        .ok_or_else(|| syn::Error::new_spanned(ast, "`inner_type` attribute is missing"))?
        .parse()
        .map_err(|_| syn::Error::new_spanned(ast, "`inner_type` attribute is not a valid path"))?;
    let expected =
        expected.ok_or_else(|| syn::Error::new_spanned(ast, "`expected` attribute is missing"))?;
    let unique_items = unique_items.unwrap_or(false);
    Ok(OneOrManyAttributes { inner_type, expected, min_length, unique_items })
}
