// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    darling::{ast, FromDeriveInput, FromField},
    proc_macro2::TokenStream as TokenStream2,
    quote::{quote, ToTokens, TokenStreamExt},
    syn,
};

pub fn impl_derive_reference_doc(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let parsed = ReferenceDocAttributes::from_derive_input(&ast).unwrap();
    let name = &parsed.ident;

    impl ToTokens for ReferenceDocAttributes {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let mut section_tokens = quote!(
                let mut s = String::new();
            );

            if let Some(doc) = get_doc_attr(&self.attrs) {
                section_tokens.append_all(quote!(
                    s.push_str(&#doc);
                    s.push_str("\n\n");
                ));
            }

            match &self.data {
                ast::Data::Struct(fields) => {
                    fields.iter().for_each(|field| {
                        section_tokens.append_all(quote!(
                            s.push_str(&#field);
                        ))
                    });
                }
                ast::Data::Enum(_) => {}
            }

            tokens.append_all(section_tokens);
            tokens.append_all(quote!(s));
        }
    }

    impl ToTokens for ReferenceDocFieldAttributes {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let name = get_ident_name(&self.ident);
            let mut ty_path = expect_typepath(&self.ty);
            let mut is_optional = false;
            let mut is_vec = false;
            if outer_type_ident_eq(&ty_path, "Option") {
                is_optional = true;
                ty_path = get_first_inner_type_from_generic(&ty_path).unwrap();
            }
            if outer_type_ident_eq(&ty_path, "Vec") {
                is_vec = true;
                ty_path = get_first_inner_type_from_generic(&ty_path).unwrap();
            }
            let ty_string = get_outer_type_without_generics(&ty_path);
            let doc = match get_doc_attr(&self.attrs) {
                None => "".to_string(),
                Some(s) => format!("{}\n\n", indent_all_markdown_headers_by(&s, 3)),
            };
            let ty_display_string = get_json_type_string_from_ty_string(&ty_string);
            tokens.append_all(quote!(format!(
                "### `{}` {{#{}}}\n\n_{}`{}{}`{}_\n\n{}",
                #name,
                #name,
                if #is_vec { "array of " } else { "" },
                #ty_display_string,
                if #is_vec { "s" } else { "" },
                if #is_optional { " (optional)" } else { "" },
                #doc,
            )));
        }
    }

    Ok(quote! {
        impl MarkdownReferenceDocGenerator for #name {
            fn get_reference_doc_markdown() -> String {
                #parsed
            }
        }
    })
}

/// Receiver struct for darling to parse macro arguments on a named struct.
#[derive(Debug, FromDeriveInput)]
#[darling(attributes(reference_doc), supports(struct_named), forward_attrs(doc))]
struct ReferenceDocAttributes {
    ident: syn::Ident,
    data: ast::Data<(), ReferenceDocFieldAttributes>,
    attrs: Vec<syn::Attribute>,
}

/// Receiver struct for darling to parse macro arguments on fields in a struct.
#[derive(Debug, FromField)]
#[darling(attributes(reference_doc), forward_attrs(doc))]
struct ReferenceDocFieldAttributes {
    ident: Option<syn::Ident>,
    ty: syn::Type,
    attrs: Vec<syn::Attribute>,
}

/// Returns true if the outer type in `p` is equal to `str`. For example, for
/// a type such as a::b::Option, this function will return true if `rhs == "Option"`.
fn outer_type_ident_eq(p: &syn::TypePath, rhs: &str) -> bool {
    p.path.segments.iter().last().unwrap().ident == rhs
}

/// Extracts a TypePath from a syn::Type, and panics for anything else.
fn expect_typepath(ty: &syn::Type) -> &syn::TypePath {
    match ty {
        syn::Type::Path(path) => path,
        _ => panic!("Not sure what to do with type: {:?}", ty),
    }
}

/// Extracts the type ident string from a TypePath.
fn get_outer_type_without_generics(path: &syn::TypePath) -> String {
    let segments: Vec<_> = path.path.segments.iter().map(|seg| seg.ident.to_string()).collect();
    segments.join("::")
}

/// Extracts the TypePath describing the contents of an AngleBracketed type.
/// Example: Option<Vec<T>> will extract the Vec<T> portion.
fn get_first_inner_type_from_generic(path: &syn::TypePath) -> Option<&syn::TypePath> {
    let args = &path.path.segments.first().unwrap().arguments;
    match &args {
        syn::PathArguments::AngleBracketed(angle_bracketed_args) => {
            if angle_bracketed_args.args.len() > 1 {
                panic!("Found multiple inner types: {:?}", args)
            }
            let first = angle_bracketed_args.args.first().unwrap();
            match &first {
                syn::GenericArgument::Type(ty) => Some(expect_typepath(ty)),
                _ => panic!("No inner type found"),
            }
        }
        syn::PathArguments::Parenthesized(_) => {
            panic!("Not sure what to do with path arguments: {:?}", args)
        }
        syn::PathArguments::None => None,
    }
}

/// Returns an appropriate JSON type string for a Rust type ident string.
fn get_json_type_string_from_ty_string(ty_string: &String) -> &str {
    if ty_string == "String" {
        "string"
    } else {
        "object"
    }
}

/// Extracts the field name from a `syn::Ident`.
fn get_ident_name(ident: &Option<syn::Ident>) -> String {
    match &ident {
        Some(val) => {
            let name = val.to_string();
            if name.starts_with("r#") {
                name.chars().skip(2).collect()
            } else {
                name
            }
        }
        None => "".to_string(),
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
