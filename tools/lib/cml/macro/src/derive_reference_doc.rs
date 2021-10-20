// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    proc_macro2::TokenStream as TokenStream2,
    quote::{quote, ToTokens, TokenStreamExt},
    std::fmt,
    syn,
};

pub fn impl_derive_reference_doc(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let attrs = parse_reference_doc_attributes(&ast)?;
    let name = ast.ident;

    impl ToTokens for ReferenceDocAttributes {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let str = format!("{}", self);
            tokens.append_all(quote!(#str))
        }
    }

    Ok(quote! {
        impl MarkdownReferenceDocGenerator for #name {
            fn get_markdown_reference_docs() -> String {
                #attrs.to_string()
            }
        }
    })
}

fn parse_reference_doc_attributes(
    ast: &syn::DeriveInput,
) -> Result<ReferenceDocAttributes, syn::Error> {
    let mut sections: Vec<ReferenceDocSectionAttributes> = vec![];
    let doc = get_doc_attr(&ast.attrs);
    if let syn::Data::Struct(s) = &ast.data {
        match &s.fields {
            syn::Fields::Named(syn::FieldsNamed { named, .. }) => {
                for field in named.iter() {
                    sections.push(parse_reference_doc_section_attributes(field)?);
                }
            }
            syn::Fields::Unnamed(..) => {
                panic!("#[derive(ReferenceDoc)] is only defined for structs with named fields");
            }
            syn::Fields::Unit => {
                panic!("#[derive(ReferenceDoc)] is only defined for structs with named fields");
            }
        };
    } else {
        panic!("#[derive(ReferenceDoc)] is only defined for structs");
    }
    Ok(ReferenceDocAttributes { doc, sections })
}

fn parse_reference_doc_section_attributes(
    field: &syn::Field,
) -> Result<ReferenceDocSectionAttributes, syn::Error> {
    Ok(ReferenceDocSectionAttributes {
        name: get_field_name(field),
        doc: get_doc_attr(&field.attrs),
    })
}

/// Attributes extracted from the `derive(ReferenceDoc)` macro.
#[derive(Debug)]
struct ReferenceDocAttributes {
    doc: Option<String>,
    sections: Vec<ReferenceDocSectionAttributes>,
}

/// Attributes to describe the documentation for a single struct field
/// for structs with `derive(ReferenceDoc)` macro.
#[derive(Debug)]
struct ReferenceDocSectionAttributes {
    name: String,
    doc: Option<String>,
}

impl fmt::Display for ReferenceDocAttributes {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let v: Vec<String> = self.sections.iter().map(|s| format!("{}", s)).collect();
        let sections_str = v.join("");
        write!(
            f,
            "# Component manifest (`.cml`) reference\n\n{}## Fields\n\n{}",
            match &self.doc {
                None => "".to_string(),
                Some(s) => format!("{}\n\n", indent_all_markdown_headers_by(&s, 1)),
            },
            sections_str,
        )
    }
}

impl fmt::Display for ReferenceDocSectionAttributes {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "### `{}` {{#{}}}\n\n{}",
            self.name,
            self.name,
            match &self.doc {
                None => "".to_string(),
                Some(s) => format!("{}\n\n", indent_all_markdown_headers_by(&s, 3)),
            }
        )
    }
}

/// Extracts the field name from a `syn::Field`.
fn get_field_name(field: &syn::Field) -> String {
    let name = field.ident.as_ref().unwrap().to_string();
    // if name[0..2].eq("r#") {
    if name.starts_with("r#") {
        name.chars().skip(2).collect()
    } else {
        name
    }
}

/// Extracts the doc comments for an `syn::Attribute` and performs some
/// simple cleanup.
fn get_doc_attr(attrs: &[syn::Attribute]) -> Option<String> {
    let attrs = attrs
        .iter()
        .filter_map(|attr| {
            if !attr.path.is_ident("doc") {
                return None;
            }

            let meta = attr.parse_meta().ok()?;
            if let syn::Meta::NameValue(syn::MetaNameValue { lit: syn::Lit::Str(s), .. }) = meta {
                return Some(s.value());
            }

            None
        })
        .collect::<Vec<_>>();

    let mut lines =
        attrs.iter().flat_map(|a| a.split('\n')).map(trim_first_space).collect::<Vec<_>>();

    if lines.last() == Some(&"".to_string()) {
        lines.pop();
    }

    if lines.is_empty() {
        None
    } else {
        Some(lines.join("\n"))
    }
}

fn trim_first_space(str: &str) -> String {
    if str.starts_with(" ") {
        str.chars().skip(1).collect()
    } else {
        str.to_string()
    }
}

fn indent_all_markdown_headers_by(str: &String, n: usize) -> String {
    str.split('\n').map(|s| indent_markdown_header_by(s, n)).collect::<Vec<_>>().join("\n")
}

fn indent_markdown_header_by(str: &str, n: usize) -> String {
    if str.starts_with("#") {
        "#".to_string().repeat(n) + &str
    } else {
        str.to_string()
    }
}
