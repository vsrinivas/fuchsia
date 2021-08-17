// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate proc_macro;

use {
    proc_macro2::TokenStream as TokenStream2,
    quote::{quote, TokenStreamExt},
    syn,
};

pub fn gen_visit_str(ty: Option<TokenStream2>, expected: &syn::LitStr) -> TokenStream2 {
    let ret = match ty {
        Some(ty) => quote!(#ty(value)),
        None => quote!(value),
    };
    quote! {
        fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            let value = value
                .parse()
                .map_err(|e| match e {
                    ParseError::InvalidValue => {
                        E::invalid_value(serde::de::Unexpected::Str(value), &#expected)
                    }
                    ParseError::InvalidLength => {
                        E::invalid_length(value.len(), &#expected)
                    }
                    e => {
                        panic!("unexpected parse error: {:?}", e);
                    }
                })?;
            Ok(#ret)
        }
    }
}

pub fn gen_visit_seq(
    ty: TokenStream2,
    inner_type: &syn::Path,
    expected: &syn::LitStr,
    min_length: Option<usize>,
    unique_items: bool,
) -> TokenStream2 {
    let inner = {
        let mut tokens = quote!();
        tokens.append_all(quote! {
            let mut elements = vec![];
            while let Some(e) = seq.next_element::<#inner_type>()? {
                elements.push(e);
            }
        });
        if let Some(min_length) = min_length {
            tokens.append_all(quote! {
                if elements.len() < #min_length {
                    return Err(serde::de::Error::invalid_length(elements.len(), &#expected));
                }
            });
        }
        if unique_items {
            tokens.append_all(quote! {
                let mut items = std::collections::HashSet::new();
                for e in &elements {
                    if !items.insert(e) {
                        return Err(serde::de::Error::invalid_value(
                            serde::de::Unexpected::Other(
                                "array with duplicate element"),
                            &#expected)
                        );
                    }
                }
                Ok(#ty(elements))
            });
        } else {
            tokens.append_all(quote! {
                Ok(#ty(elements))
            });
        }
        tokens
    };
    let mut tokens = quote!();
    tokens.append_all(quote! {
        fn visit_seq<A>(self, mut seq: A) -> Result<Self::Value, A::Error>
        where
            A: serde::de::SeqAccess<'de>
        {
            #inner
        }
    });
    tokens
}

pub fn extract_inner_type(
    ast: &syn::DeriveInput,
    attr: syn::MetaNameValue,
    inner_type: &mut Option<syn::LitStr>,
) -> Result<(), syn::Error> {
    match attr.lit {
        syn::Lit::Str(l) => {
            if inner_type.is_some() {
                return Err(syn::Error::new_spanned(ast, "duplicate `inner_type` attribute"));
            }
            *inner_type = Some(l);
        }
        _ => {
            return Err(syn::Error::new_spanned(
                ast,
                "`inner_type` attribute value must be string",
            ));
        }
    }
    Ok(())
}

pub fn extract_min_length(
    ast: &syn::DeriveInput,
    attr: syn::MetaNameValue,
    min_length: &mut Option<usize>,
) -> Result<(), syn::Error> {
    match attr.lit {
        syn::Lit::Int(l) => {
            if min_length.is_some() {
                return Err(syn::Error::new_spanned(ast, "duplicate `min_length` attribute"));
            }
            let l: usize = l.base10_parse().map_err(|_| {
                syn::Error::new_spanned(ast, "`min_length` attribute is not base 10")
            })?;
            *min_length = Some(l);
        }
        _ => {
            return Err(syn::Error::new_spanned(ast, "`min_length` attribute value must be int"));
        }
    }
    Ok(())
}

pub fn extract_unique_items(
    ast: &syn::DeriveInput,
    attr: syn::MetaNameValue,
    unique_items: &mut Option<bool>,
) -> Result<(), syn::Error> {
    match attr.lit {
        syn::Lit::Bool(b) => {
            if unique_items.is_some() {
                return Err(syn::Error::new_spanned(ast, "duplicate `unique_items` attribute"));
            }
            *unique_items = Some(b.value);
        }
        _ => {
            return Err(syn::Error::new_spanned(
                ast,
                "`unique_items` attribute value must be bool",
            ));
        }
    }
    Ok(())
}

pub fn ident_from_path(path: &syn::Path) -> String {
    path.get_ident().map(|i| i.to_string()).unwrap_or_else(|| String::new())
}

pub fn extract_expected(
    ast: &syn::DeriveInput,
    attr: syn::MetaNameValue,
    expected: &mut Option<syn::LitStr>,
) -> Result<(), syn::Error> {
    match attr.lit {
        syn::Lit::Str(l) => {
            if expected.is_some() {
                return Err(syn::Error::new_spanned(ast, "duplicate `expected` attribute"));
            }
            *expected = Some(l);
        }
        _ => {
            return Err(syn::Error::new_spanned(ast, "`expected` attribute value must be string"));
        }
    }
    Ok(())
}
