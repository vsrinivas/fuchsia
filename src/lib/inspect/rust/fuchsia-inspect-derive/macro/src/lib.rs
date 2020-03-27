// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do not depend on this crate. Instead, use the `fuchsia_inspect_derive` crate.

#![recursion_limit = "128"]

extern crate proc_macro;

use proc_macro::TokenStream;
use quote::{format_ident, quote, quote_spanned};
use std::convert::TryFrom;
use syn::spanned::Spanned;
use syn::{parse_macro_input, DeriveInput, Error};

struct InspectField {
    /// Name of the original and the inspect data field.
    name: syn::Ident,

    /// The type path of the original field. For instance a `u32` has a single segment
    /// `u32`, and `my::Yak` has two segments `my` and `Yak`.
    type_path: syn::TypePath,
}

impl InspectField {
    /// Get a string literal containing the name of the field.
    fn literal(&self) -> syn::LitStr {
        syn::LitStr::new(self.name.to_string().as_ref(), self.name.span())
    }

    /// Convenience method to get the fully qualified path to the inspect trait.
    fn inspect_path(&self) -> proc_macro2::TokenStream {
        let type_path = &self.type_path;
        quote! { <#type_path as ::fuchsia_inspect_derive::Inspect> }
    }

    /// Creates a single field declaration for the inspect data struct declaration.
    fn struct_decl(&self) -> proc_macro2::TokenStream {
        let name = &self.name;
        let span = self.type_path.span();
        let inspect = self.inspect_path();
        quote_spanned! { span=> #name: #inspect::Data }
    }

    /// Creates a single field assignment for the inspect data struct initialization.
    ///
    /// Note that the local variable `inspect_derive_node` must be defined.
    fn create_struct_expr(&self) -> proc_macro2::TokenStream {
        let literal = self.literal();
        let name = &self.name;
        let inspect = self.inspect_path();
        quote! { #name: #inspect::inspect_create(&self.#name, &inspect_derive_node, #literal) }
    }

    /// Creates a single field update assignment statement.
    fn update_stmt(&self) -> proc_macro2::TokenStream {
        let name = &self.name;
        let inspect = self.inspect_path();
        quote! {#inspect::inspect_update(&self.#name, &mut data.#name)}
    }
}

/// Parse a syn::Field into an inspect field. Returns an error if the field is not named.
impl TryFrom<&syn::Field> for InspectField {
    type Error = Error;

    fn try_from(f: &syn::Field) -> Result<InspectField, Error> {
        let name = f.ident.as_ref().expect("internal error: expected named field").clone();
        let type_path = to_type_path(&f.ty)?;
        Ok(InspectField { name, type_path })
    }
}

/// Convenience method to convert a type to a type path. Errors if not a type path.
fn to_type_path(ty: &syn::Type) -> Result<syn::TypePath, Error> {
    match ty {
        syn::Type::Path(ref p) => Ok(p.clone()),
        _ => Err(Error::new_spanned(ty, "cannot derive inspect for this type")),
    }
}

/// Parsed inspect attributes for an individual field.
#[derive(Default)]
struct FieldAttrArgs {
    /// The field should not be inspected.
    /// Example: [inspect(skip)]
    skip: bool,
}

/// Given an `inspect` field attribute, parse its arguments. Errors out if arguments are
/// malformed.
fn parse_field_attr_args(attr: &syn::Attribute, args: &mut FieldAttrArgs) -> Result<(), Error> {
    return match attr.parse_meta()? {
        syn::Meta::List(syn::MetaList { ref nested, .. }) => {
            for nested_meta in nested {
                match nested_meta {
                    syn::NestedMeta::Meta(syn::Meta::Path(ref path)) => {
                        if path.is_ident("skip") {
                            args.skip = true;
                        } else {
                            return Err(Error::new_spanned(
                                path,
                                "unrecognized attribute argument",
                            ));
                        }
                    }
                    _ => {
                        return Err(Error::new_spanned(
                            nested_meta,
                            "unrecognized attribute argument",
                        ))
                    }
                }
            }
            Ok(())
        }
        meta @ _ => Err(Error::new_spanned(meta, "unrecognized attribute arguments")),
    };
}

/// Returns the field attribute args on a structured form, or an error upon unrecognized arguments.
/// Ignores any attributes that are not `inspect`.
fn get_field_attrs(f: &syn::Field) -> Result<FieldAttrArgs, Error> {
    let mut args = FieldAttrArgs::default();
    for attr in &f.attrs {
        if attr.path.is_ident("inspect") {
            parse_field_attr_args(attr, &mut args)?;
        }
    }
    Ok(args)
}

/// Checks that no `inspect` attributes are set on the container.
fn check_container_attrs(d: &syn::DeriveInput) -> Result<(), Error> {
    for attr in &d.attrs {
        if attr.path.segments.len() == 1 && attr.path.segments[0].ident == "inspect" {
            return Err(Error::new_spanned(attr, "inspect does not support container attributes"));
        }
    }
    Ok(())
}

/// The `Inspect` derive macro.
#[proc_macro_derive(Inspect, attributes(inspect))]
pub fn derive(input: TokenStream) -> TokenStream {
    let ast = parse_macro_input!(input as DeriveInput);
    match derive_inner(ast) {
        Ok(token_stream) => token_stream,
        Err(err) => err.to_compile_error(),
    }
    .into()
}

fn derive_inner(ast: DeriveInput) -> Result<proc_macro2::TokenStream, Error> {
    let name = &ast.ident;
    check_container_attrs(&ast)?;
    let fields = match ast.data {
        syn::Data::Struct(syn::DataStruct {
            fields: syn::Fields::Named(syn::FieldsNamed { ref named, .. }),
            ..
        }) => Ok(named),
        _ => Err(Error::new_spanned(&ast, "can only derive inspect on named structs")),
    }?;
    let mut inspect_fields = Vec::new();
    for field in fields {
        let args = get_field_attrs(field)?;
        if args.skip {
            continue;
        }
        let data_field = InspectField::try_from(field)?;
        inspect_fields.push(data_field);
    }

    let inspect_data_ident = format_ident!("{}InspectData", name);
    let struct_decls = inspect_fields.iter().map(|f| f.struct_decl());
    let create_struct_exprs = inspect_fields.iter().map(|f| f.create_struct_expr());
    let update_stmts = inspect_fields.iter().map(|f| f.update_stmt());

    Ok(quote! {
        struct #inspect_data_ident {
            #(#struct_decls,)*
            inspect_derive_node: ::fuchsia_inspect::Node,
        }

        impl fuchsia_inspect_derive::Inspect for #name {

            type Data = #inspect_data_ident;

            fn inspect_create<T: AsRef<str>>(
                &self,
                parent: &::fuchsia_inspect::Node,
                name: T
            ) -> Self::Data {
                let inspect_derive_node = parent.create_child(name);
                #inspect_data_ident {
                    #(#create_struct_exprs,)*
                    inspect_derive_node,
                }
            }

            fn inspect_update(&self, data: &mut Self::Data) {
                #(#update_stmts;)*
            }
        }
    })
}
