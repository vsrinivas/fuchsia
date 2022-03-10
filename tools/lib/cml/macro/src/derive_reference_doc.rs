// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    darling::{ast, FromDeriveInput, FromField, FromMeta},
    proc_macro2::TokenStream as TokenStream2,
    quote::{quote, ToTokens, TokenStreamExt},
    syn,
};

pub fn impl_derive_reference_doc(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let mut parsed = ReferenceDocAttributes::from_derive_input(&ast).unwrap();
    let name = &parsed.ident;
    let doc = get_doc_attr(&parsed.attrs);
    let indent_headers = parsed
        .indent_headers
        .unwrap_or_else(|| doc.map(|docstr| get_last_markdown_header_depth(&docstr)).unwrap_or(0));

    // Forward the struct-level `fields_as` value and optionally `indent_headers` to all field attributes.
    match &mut parsed.data {
        ast::Data::Struct(fields) => {
            for field in &mut fields.fields {
                field.fields_as = parsed.fields_as.clone();
                match &mut field.indent_headers {
                    Some(_) => {}
                    None => {
                        field.indent_headers = Some(indent_headers);
                    }
                }
            }
        }
        ast::Data::Enum(_) => {}
    }

    impl ToTokens for ReferenceDocAttributes {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let mut section_tokens = quote!(
                let mut s = String::new();
            );

            let top_level_doc_after_fields = self.top_level_doc_after_fields.unwrap_or_default();
            let top_level_doc = if let Some(doc) = get_doc_attr(&self.attrs) {
                format!("{}\n\n", doc)
            } else {
                "".to_string()
            };

            if !top_level_doc_after_fields {
                section_tokens.append_all(quote!(s.push_str(&#top_level_doc);));
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

            if top_level_doc_after_fields {
                section_tokens.append_all(quote!(s.push_str(&#top_level_doc);));
            }

            tokens.append_all(section_tokens);
            tokens.append_all(quote!(s));
        }
    }

    impl ToTokens for ReferenceDocFieldAttributes {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let name = get_ident_name(&self.ident);
            let indent_headers = self.indent_headers.unwrap_or(0);
            let mut rust_ty_path = expect_typepath(&self.ty);
            let mut is_optional = false;
            let mut is_vec = false;
            if outer_type_ident_eq(&rust_ty_path, "Option") {
                is_optional = true;
                rust_ty_path = get_first_inner_type_from_generic(&rust_ty_path).unwrap();
            }
            if outer_type_ident_eq(&rust_ty_path, "Vec") {
                is_vec = true;
                rust_ty_path = get_first_inner_type_from_generic(&rust_ty_path).unwrap();
            }

            let rust_ty_string = get_outer_type_without_generics(&rust_ty_path);
            let rust_ty_ident = quote::format_ident!("{}", rust_ty_string);
            // Get the json-equivalent value type for this Rust type.
            let json_type_string = get_json_type_string_from_field_attrs(&self, &rust_ty_string);
            match &self.fields_as {
                FieldOutputType::Headings => {
                    let doc = get_doc_attr(&self.attrs)
                        .map(|s| indent_all_markdown_headers_by(&s, indent_headers + 1))
                        .unwrap_or_default();
                    let trait_output = if self.recurse {
                        quote!(
                            #rust_ty_ident::get_reference_doc_markdown_with_options(#indent_headers + 1, 0)
                        )
                    } else {
                        quote!("")
                    };

                    let indented_format_string =
                        indent_markdown_header_by("# `{name}` {{#{name}}}\n\n", indent_headers);
                    tokens.append_all(quote!({
                        let doc = #doc.to_string();
                        let trait_output = #trait_output.to_string();
                        let mut output = format!(#indented_format_string, name=#name);
                        output.push_str("_");
                        if #is_vec {
                            output.push_str("array of ");
                        }
                        output.push_str("`");
                        output.push_str(#json_type_string);
                        output.push_str("`");
                        if #is_optional {
                            output.push_str(" (optional)");
                        }
                        output.push_str("_\n\n");
                        if !doc.is_empty() {
                            output.push_str(&doc);
                            output.push_str("\n\n");
                        }
                        if !trait_output.is_empty() {
                            output.push_str(&trait_output);
                            output.push_str("\n\n");
                        }
                        output
                    }));
                }
                FieldOutputType::List => {
                    let doc = get_doc_attr(&self.attrs)
                        .map(|s| indent_lines_with_spaces(&s, 2, 1))
                        .unwrap_or_default();
                    let trait_output = if self.recurse {
                        quote!(
                            #rust_ty_ident::get_reference_doc_markdown_with_options(#indent_headers, 2)
                        )
                    } else {
                        quote!("")
                    };
                    // FieldOutputType::List
                    tokens.append_all(quote!({
                        let trait_output = #trait_output.to_string();
                        let mut output = format!("- `{}`: (_", #name);
                        if #is_optional {
                            output.push_str("optional ");
                        }
                        if #is_vec {
                            output.push_str("array of ");
                        }
                        output.push_str("`");
                        output.push_str(#json_type_string);
                        output.push_str("`_) ");
                        output.push_str(#doc);
                        if !trait_output.is_empty() {
                            output.push_str("\n   ");
                            output.push_str(&trait_output);
                        }
                        output.push_str("\n");
                        output
                    }));
                }
            }
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

    /// Instructs the doc generator to indent any markdown headers encountered
    /// on fields with this many additional hash (#) marks.
    ///
    /// A default value is derived by looking at the top-level doc comment
    /// and extracting header depth.
    #[darling(default)]
    indent_headers: Option<usize>,

    /// Instructs the doc generator to output struct fields as a list or
    /// a header.
    #[darling(default)]
    fields_as: FieldOutputType,

    /// Instructs the doc generator to place the struct's top-level doc comment
    /// after the fields' doc comments.
    #[darling(default)]
    top_level_doc_after_fields: Option<bool>,
}

#[derive(Debug, Clone, FromMeta)]
#[darling(rename_all = "lowercase")]
enum FieldOutputType {
    Headings,
    List,
}

impl Default for FieldOutputType {
    fn default() -> FieldOutputType {
        FieldOutputType::Headings
    }
}

/// Receiver struct for darling to parse macro arguments on fields in a struct.
#[derive(Debug, FromField)]
#[darling(attributes(reference_doc), forward_attrs(doc))]
struct ReferenceDocFieldAttributes {
    ident: Option<syn::Ident>,
    ty: syn::Type,
    attrs: Vec<syn::Attribute>,

    /// If specified, the JSON value type for this field. For example: "string",
    /// "object", "number", "boolean".
    ///
    /// If omitted, a naive type will be derived from the Rust type:
    ///   String -> string
    ///   bool -> boolean
    ///   u8, u16, i8, ... -> number
    ///   anything with `recursive=true` -> object
    ///   default -> string
    #[darling(default)]
    json_type: Option<String>,

    /// Instructs the doc generator to retrieve markdown by calling
    /// `get_reference_doc_markdown()` on the inner type of the field.
    #[darling(default)]
    recurse: bool,

    /// Instructs the doc generator to indent any markdown headers encountered
    /// with this many additional hash (#) marks.
    #[darling(default)]
    indent_headers: Option<usize>,

    /// Forwarded from `ReferenceDocAttributes.fields_as`.
    #[darling(skip)]
    fields_as: FieldOutputType,
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

/// Returns an appropriate JSON type string from parsed token attributes.
fn get_json_type_string_from_field_attrs(
    attrs: &ReferenceDocFieldAttributes,
    rust_ty_string: &str,
) -> String {
    attrs.json_type.clone().unwrap_or_else(|| {
        get_json_type_string_from_ty_string(&rust_ty_string)
            .unwrap_or_else(|| if attrs.recurse { "object" } else { "string" })
            .to_string()
    })
}

/// Returns an appropriate JSON type string for a Rust type ident string.
fn get_json_type_string_from_ty_string(ty_string: &str) -> Option<&str> {
    let number_types = &[
        "i8", "i16", "i32", "i64", "i128", "u8", "u16", "u32", "u64", "u128", "usize", "f32", "f64",
    ];
    let map_types = &["Map", "BTreeMap", "HashMap"];
    if ty_string == "String" {
        Some("string")
    } else if map_types.iter().any(|v| v == &ty_string) {
        Some("object")
    } else if number_types.iter().any(|v| v == &ty_string) {
        Some("number")
    } else {
        None
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

fn get_last_markdown_header_depth(s: &str) -> usize {
    let last = s.split('\n').filter(|s| s.starts_with('#')).last();
    last.map(|h| h.chars().take_while(|c| c == &'#').count()).unwrap_or(0)
}

fn indent_all_markdown_headers_by(s: &str, n: usize) -> String {
    if n == 0 {
        s.to_string()
    } else {
        s.split('\n').map(|part| indent_markdown_header_by(part, n)).collect::<Vec<_>>().join("\n")
    }
}

fn indent_markdown_header_by(s: &str, n: usize) -> String {
    if s.starts_with("#") {
        "#".to_string().repeat(n) + &s
    } else {
        s.to_string()
    }
}

fn indent_lines_with_spaces(s: &str, n: usize, ignore_first: usize) -> String {
    if n == 0 {
        s.to_string()
    } else {
        let prefix = " ".to_string().repeat(n);
        s.split('\n')
            .enumerate()
            .map(
                |(i, part)| {
                    if i < ignore_first {
                        part.to_string()
                    } else {
                        prefix.clone() + part
                    }
                },
            )
            .collect::<Vec<_>>()
            .join("\n")
    }
}
