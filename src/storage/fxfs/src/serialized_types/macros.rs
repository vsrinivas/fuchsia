// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate quote;

use proc_macro::TokenStream;
use quote::ToTokens;
use std::collections::BTreeMap;
use syn::parse::{Parse, ParseStream};
use syn::{parse_macro_input, Result};

/// Holds an open-ended version range like `3..` meaning version 3 and up.
#[derive(Clone)]
struct PatOpenVersionRange {
    pub lo: syn::LitInt,
    pub dots: syn::token::Dot2,
}
impl PatOpenVersionRange {
    pub fn lo_value(&self) -> u32 {
        self.lo.base10_parse::<u32>().unwrap_or(0)
    }
}
impl std::cmp::PartialEq for PatOpenVersionRange {
    fn eq(&self, other: &Self) -> bool {
        self.lo_value() == other.lo_value()
    }
}
impl std::cmp::PartialOrd for PatOpenVersionRange {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.lo_value().partial_cmp(&other.lo_value())
    }
}
impl Parse for PatOpenVersionRange {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        Ok(Self { lo: input.parse()?, dots: input.parse()? })
    }
}
impl ToTokens for PatOpenVersionRange {
    fn to_tokens(&self, tokens: &mut proc_macro2::TokenStream) {
        self.lo.to_tokens(tokens);
        self.dots.to_tokens(tokens);
    }
}

/// Holds a "fat arrow" mapping from version range to type. e.g. `3.. => FooV3,`
struct Arm {
    pub pat: PatOpenVersionRange,
    pub _fat_arrow_token: syn::token::FatArrow,
    pub ident: syn::Ident,
    pub _comma: Option<syn::token::Comma>,
}
impl Parse for Arm {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        Ok(Self {
            pat: input.parse()?,
            _fat_arrow_token: input.parse()?,
            ident: input.parse()?,
            _comma: input.parse()?,
        })
    }
}

struct Input {
    arms: Vec<Arm>,
}
impl Parse for Input {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        let mut arms = Vec::new();
        while !input.is_empty() {
            // Note: We can't use syn::Pat to parse out a range because it doesn't seem to support
            // open ranges right now (https://docs.rs/syn/1.0.86/src/syn/pat.rs.html#563) so we
            // roll our own above.
            arms.push(input.call(Arm::parse)?);
        }
        Ok(Self { arms })
    }
}

/// Implements traits for versioned structures.
/// Namely:
///   * [Versioned] for all versions.
///   * [VersionedLatest] for the most recent version.
///   * Transitive [From] for any version to a newer version.
#[proc_macro]
pub fn versioned_type(input: TokenStream) -> TokenStream {
    let arms = parse_macro_input!(input as Input).arms;

    let versions: BTreeMap<u32, syn::Ident> =
        arms.iter().map(|x| (x.pat.lo_value(), x.ident.clone())).collect();
    assert_eq!(arms.len(), versions.len(), "Duplicate version range found.");

    let mut out = quote! {};

    // The latest version should implement VersionedLatest.
    if let Some((major, ident)) = versions.iter().last() {
        // We order versions in their original match order.
        let iter = arms
            .iter()
            .map(|x| (x.pat.lo_value(), x.ident.clone()))
            .map(|(v, i)| quote! { #v.. => Ok(#i::deserialize_from(reader, version)?.into()), });
        out = quote! {
            #out
            impl VersionedLatest for #ident {
                fn deserialize_from_version<R>(
                    reader: &mut R, version: Version) -> anyhow::Result<Self>
                where R: std::io::Read, Self: Sized {
                    assert!(#major <= LATEST_VERSION.major,
                        "Found version > LATEST_VERSION for {}.", stringify!(#ident));
                    const future_ver : u32 = LATEST_VERSION.major + 1;
                    match version.major {
                        future_ver.. => anyhow::bail!(format!(
                                "Invalid future version {} > {} deserializing {}.",
                                version, LATEST_VERSION, stringify!(#ident))),
                        #(#iter)*
                        x => anyhow::bail!(format!(
                                "Unsupported version {} for {}.", x, stringify!(#ident))),
                    }
                }
            }
        };
    }

    // The [From] ladder is a little tricky because users are free to repeat types
    // in their version mapping. We use our sequence of versions.
    let mut idents: Vec<&syn::Ident> = versions.values().collect();
    idents.dedup();
    let last_ident = idents.pop().unwrap();

    for i in 0..idents.len().saturating_sub(1) {
        let ident = &idents[i];
        let next_ident = &idents[i + 1];
        out = quote! {
            #out
            impl From<#ident> for #last_ident {
                fn from(item: #ident) -> Self {
                    let tmp: #next_ident = item.into();
                    tmp.into()
                }
            }
        }
    }
    TokenStream::from(out)
}

/// This is just a shorthand for `impl Versioned for Foo {}` which is required for all top-level
/// versioned struct/enum in Fxfs.
#[proc_macro_derive(Versioned)]
pub fn derive_versioned(input: TokenStream) -> TokenStream {
    let syn::DeriveInput { ident, .. } = parse_macro_input!(input);
    TokenStream::from(quote! { impl Versioned for #ident {} })
}
