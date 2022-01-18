// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;
#[macro_use]
extern crate quote;

use proc_macro::TokenStream;
use std::collections::BTreeMap;
use syn::parse::{Parse, ParseStream};
use syn::{parse_macro_input, Result};

// TODO(ripper): Consider supporting a richer pattern language using ranges like:
//
//   2 =>     MyTypeV2,
//   3..=5 => MyTypeV3,
//
// There are limits to how far we can go with this approach, though. We can't evaluate expressions
// that reference outside the macro, for instance. I believe this makes it impossible to use const
// expressions here such as:
//
//   2 =>          MyTypeV2,
//   3..=LATEST => MyTypeV3,
//
// We need to know the numerical latest version in order to support serialization so these changes
// are likely going to require some reworking of our macros.

struct Input {
    arms: Vec<syn::Arm>,
}
impl Parse for Input {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        let mut arms = Vec::new();
        while !input.is_empty() {
            arms.push(input.call(syn::Arm::parse)?);
        }
        Ok(Self { arms })
    }
}

/// Implements traits for versioned structures.
/// Namely:
///   * [Version] for all versions.
///   * [VersionLatest] for the most recent version.
///   * Transitive [From] for any version to a newer version.
#[proc_macro]
pub fn versioned_type(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as Input);

    let mut out = quote! {};

    let versions: Vec<(syn::Pat, syn::Ident)> = input
        .arms
        .iter()
        .map(|arm| {
            if let syn::Expr::Path(path) = &*arm.body {
                let syn::ExprPath { path, .. } = path;
                if let Some(ident) = path.get_ident() {
                    (arm.pat.clone(), ident.clone())
                } else {
                    panic!("Type names should not include path segments: {:?}.", path)
                }
            } else {
                panic!("Expected a type name, found {:?}", arm.body)
            }
        })
        .collect();

    // It's possible for a user to repeat a struct like:
    //   1 => FooV1,
    //   2 => FooV1,
    //   3 => FooV2,
    // In this case FooV1::version() returns 2.
    //
    // We can't necessarily interpret the pattern from within the macro -- it may contain
    // expressions with external paths. Instead, we do our best to produce code that will be
    // optimized away in version() below.
    let mut ident_versions: BTreeMap<&syn::Ident, Vec<&syn::Pat>> = BTreeMap::new();
    for (version, ident) in &versions {
        if !ident_versions.contains_key(&ident) {
            ident_versions.insert(&ident, Vec::new());
        }
        let v = ident_versions.get_mut(&ident).unwrap();
        v.push(version);
    }

    for (ident, versions) in &ident_versions {
        let count = versions.len();
        out = quote! {
            #out
            impl Version for #ident {
                fn version() -> u32 {
                    let versions : [u32; #count] = [ #(#versions),* ];
                    *versions.iter().max().unwrap()
                }
            }
        };
    }
    if let Some((_, ident)) = versions.last() {
        let mut body = quote! {};
        for (version, ident) in &versions {
            body = quote! {
                #body
                #version => Ok(#ident::deserialize_from(reader)?.into()),
            }
        }
        out = quote! {
            #out
            impl VersionLatest for #ident {
                fn deserialize_from_version<R>(reader: &mut R, version: u32) -> anyhow::Result<Self>
                where R: std::io::Read, Self: Sized {
                    match version {
                        #body
                        _ => anyhow::bail!(format!("Invalid version {} for #ident.", version)),
                    }
                }
            }
        };
    }

    // The [From] ladder is a little tricky because users are free to repeat types
    // in their version mapping. We first have to build a sequence of versions.
    let mut idents: Vec<&syn::Ident> = versions.iter().map(|(_, i)| i).collect();
    idents.sort();
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
