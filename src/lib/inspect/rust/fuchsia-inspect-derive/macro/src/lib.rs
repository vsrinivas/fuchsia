// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do not depend on this crate. Instead, use the `fuchsia_inspect_derive` crate.

#![recursion_limit = "128"]

extern crate proc_macro;

use proc_macro2::{Span, TokenStream};
use quote::{format_ident, quote, quote_spanned};
use syn::spanned::Spanned;
use syn::{parse_macro_input, DeriveInput, Error};

struct UnitField {
    /// Name of the original and the inspect data field.
    name: syn::Ident,

    /// The type path of the original field. For instance a `u32` has a single segment
    /// `u32`, and `my::Yak` has two segments `my` and `Yak`.
    type_path: syn::TypePath,

    /// Field attribute arguments, e.g. `skip`
    attr_args: FieldAttrArgs,
}

impl UnitField {
    /// Parse a syn::Field into a unit field. Returns an error if the field is not named
    /// and Ok(None) if the field should be skipped.
    fn try_from_field(f: &syn::Field) -> Result<Option<Self>, Error> {
        let name = f.ident.as_ref().expect("internal error: expected named field").clone();
        let attr_args = get_field_attrs(f)?;
        attr_args.validate_for_unit()?;
        if attr_args.skip {
            Ok(None)
        } else {
            let type_path = to_type_path(&f.ty)?;
            Ok(Some(UnitField { name, type_path, attr_args }))
        }
    }

    /// Get a string literal containing the name of the field.
    fn literal(&self) -> syn::LitStr {
        self.attr_args
            .rename
            .clone()
            .unwrap_or_else(|| syn::LitStr::new(self.name.to_string().as_ref(), self.name.span()))
    }

    /// Convenience method to get the fully qualified path to the Unit trait.
    fn unit_path(&self) -> TokenStream {
        let type_path = &self.type_path;
        quote! { <#type_path as ::fuchsia_inspect_derive::Unit> }
    }

    /// Creates a single field declaration for the inspect data struct declaration.
    fn struct_decl(&self) -> TokenStream {
        let name = &self.name;
        let span = self.type_path.span();
        let unit = self.unit_path();
        quote_spanned! { span=> #name: #unit::Data }
    }

    /// Creates a single field assignment for the inspect data struct initialization.
    ///
    /// Note that the local variable `inspect_node` must be defined.
    fn create_struct_expr(&self) -> TokenStream {
        let literal = self.literal();
        let name = &self.name;
        let unit = self.unit_path();
        quote! { #name: #unit::inspect_create(&self.#name, &inspect_node, #literal) }
    }

    /// Creates a single field update assignment statement.
    fn update_stmt(&self) -> TokenStream {
        let name = &self.name;
        let unit = self.unit_path();
        quote! {#unit::inspect_update(&self.#name, &mut data.#name)}
    }
}

struct InspectField {
    /// Name of the original and the inspect data field.
    name: syn::Ident,

    /// Field attribute arguments, e.g. `skip`
    attr_args: FieldAttrArgs,
}

impl InspectField {
    /// Parse a syn::Field into an inspect field. Returns an error if the field is not named
    /// and Ok(None) if the field should be skipped.
    fn try_from_field(f: &syn::Field) -> Result<Option<Self>, Error> {
        let name = f.ident.as_ref().expect("internal error: expected named field").clone();
        let attr_args = get_field_attrs(f)?;
        attr_args.validate_for_inspect()?;
        if attr_args.skip {
            Ok(None)
        } else {
            Ok(Some(InspectField { name, attr_args }))
        }
    }

    /// Get a string literal containing the name of the field.
    fn literal(&self) -> syn::LitStr {
        self.attr_args
            .rename
            .clone()
            .unwrap_or_else(|| syn::LitStr::new(self.name.to_string().as_ref(), self.name.span()))
    }

    /// Creates an iattach assignment.
    fn iattach_stmt(&self, has_inspect_node: bool) -> Result<TokenStream, Error> {
        let name = &self.name;
        let span = name.span();

        match (self.attr_args.forward, has_inspect_node) {
            // attach to parent
            (false, false) => {
                let literal = self.literal();
                Ok(quote_spanned! { span=> self.#name.iattach(parent, #literal)? })
            }

            // attach to self.inspect_node
            (false, true) => {
                let literal = self.literal();
                Ok(quote_spanned! { span=> self.#name.iattach(&self.inspect_node, #literal)? })
            }

            // forward attachment to inner field
            (true, false) => Ok(quote_spanned! { span=> self.#name.iattach(parent, &name)? }),

            // forward and attach to inspect_node would cause a name collision
            (true, true) => Err(Error::new_spanned(
                name,
                "inspect_node and inspect(forward) cannot be enabled simultaneously",
            )),
        }
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
struct FieldAttrArgs {
    /// The span of the field, for errors.
    span: Span,

    /// The field should not be inspected.
    /// Example: [inspect(skip)]
    skip: bool,

    /// Forwards inspect attachments to this field. Only available for types
    /// without an `inspect_node`. As a result, the name of this field will go
    /// unused. Useful for wrapper types.
    /// Example: [inspect(forward)]
    forward: bool,

    /// Renames the inspect field to a different name.
    /// Example: [inspect(rename = "foo")]
    rename: Option<syn::LitStr>,
}

impl FieldAttrArgs {
    /// Create from a span used for error messages. All fields assume their default values.
    fn new(span: Span) -> Self {
        Self { span, skip: false, forward: false, rename: None }
    }

    /// Validate that attributes are valid for `Unit`
    fn validate_for_unit(&self) -> Result<(), Error> {
        self.validate_skip()?;
        if self.forward {
            Err(Error::new(self.span, "inspect(forward) is not defined for `Unit`"))
        } else {
            Ok(())
        }
    }

    /// Validate that attributes are valid for `Inspect`
    fn validate_for_inspect(&self) -> Result<(), Error> {
        self.validate_skip()?;
        if self.forward && self.rename.is_some() {
            Err(Error::new(
                self.span,
                concat!(
                    "inspect(rename) cannot be used with inspect(forward) ",
                    "since forward ignores any name provided"
                ),
            ))
        } else {
            Ok(())
        }
    }

    /// Validate that if `skip` is provided, no other attributes are.
    fn validate_skip(&self) -> Result<(), Error> {
        if self.skip && (self.forward || self.rename.is_some()) {
            Err(Error::new(
                self.span,
                "inspect(skip) cannot be specified together with other attributes",
            ))
        } else {
            Ok(())
        }
    }
}

/// Given an `inspect` field attribute, parse its arguments. Errors out if
/// arguments are malformed.
fn parse_field_attr_args(attr: &syn::Attribute, args: &mut FieldAttrArgs) -> Result<(), Error> {
    return match attr.parse_meta()? {
        syn::Meta::List(syn::MetaList { ref nested, .. }) => {
            for nested_meta in nested {
                match nested_meta {
                    syn::NestedMeta::Meta(syn::Meta::Path(ref path)) => {
                        if path.is_ident("skip") {
                            args.skip = true;
                        } else if path.is_ident("forward") {
                            args.forward = true;
                        } else {
                            return Err(Error::new_spanned(
                                path,
                                "unrecognized attribute argument",
                            ));
                        }
                    }
                    syn::NestedMeta::Meta(syn::Meta::NameValue(ref name_value)) => {
                        if name_value.path.is_ident("rename") {
                            if let syn::Lit::Str(ref lit_str) = name_value.lit {
                                args.rename = Some(lit_str.clone());
                            } else {
                                return Err(Error::new_spanned(
                                    &name_value.lit,
                                    "rename value must be string literal",
                                ));
                            }
                        } else {
                            return Err(Error::new_spanned(
                                name_value,
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
    let mut args = FieldAttrArgs::new(f.span());
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

/// The `Unit` derive macro. Requires that the type is a named struct.
/// Type- and lifetime parameters are supported if they are ignored.
///
/// The only supported field-level attribute is `inspect(skip)`.
// TODO(fxbug.dev/50504): Add support for more types, such as enums.
#[proc_macro_derive(Unit, attributes(inspect))]
pub fn derive_unit(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let ast = parse_macro_input!(input as DeriveInput);
    match derive_unit_inner(ast) {
        Ok(token_stream) => token_stream,
        Err(err) => err.to_compile_error(),
    }
    .into()
}

fn derive_unit_inner(ast: DeriveInput) -> Result<TokenStream, Error> {
    let name = &ast.ident;
    check_container_attrs(&ast)?;
    let fields = match ast.data {
        syn::Data::Struct(syn::DataStruct {
            fields: syn::Fields::Named(syn::FieldsNamed { ref named, .. }),
            ..
        }) => Ok(named),
        _ => Err(Error::new_spanned(&ast, "can only derive Unit on named structs")),
    }?;
    let mut unit_fields = Vec::new();
    for field in fields {
        if let Some(data_field) = UnitField::try_from_field(field)? {
            unit_fields.push(data_field);
        }
    }

    let inspect_data_ident = format_ident!("_{}InspectData", name);
    let struct_decls = unit_fields.iter().map(|f| f.struct_decl());

    let (impl_generics, ty_generics, where_clause) = ast.generics.split_for_impl();
    let create_struct_exprs = unit_fields.iter().map(|f| f.create_struct_expr());
    let update_stmts = unit_fields.iter().map(|f| f.update_stmt());

    Ok(quote! {
        #[derive(Default)]
        struct #inspect_data_ident {
            #(#struct_decls,)*
            inspect_node: ::fuchsia_inspect_derive::InspectNode,
        }

        impl #impl_generics ::fuchsia_inspect_derive::Unit for #name #ty_generics #where_clause {

            type Data = #inspect_data_ident;

            fn inspect_create(
                &self,
                parent: &::fuchsia_inspect_derive::InspectNode,
                name: impl AsRef<str>
            ) -> Self::Data {
                let inspect_node = parent.create_child(name);
                #inspect_data_ident {
                    #(#create_struct_exprs,)*
                    inspect_node,
                }
            }

            fn inspect_update(&self, data: &mut Self::Data) {
                #(#update_stmts;)*
            }
        }
    })
}

/// The `Inspect` derive macro. Requires that the type is a named struct.
///
/// If an `inspect_node` field is present, an inspect node will be created
/// and used for all properties of this node. It must be of type
/// `fuchsia_inspect::Node`. If `inspect_node` is NOT present, all
/// `Inspect` fields of this type will be merged into the parent node
/// (similar to serde's "flatten" attribute).
/// As a result, the name provided by the parent will be ignored (unless
/// `inspect(forward)` is used, see below).
/// This is suitable when an intermediate layer should be omitted. Care should
/// be taken to (a) avoid name collisions between nested inspect fields of
/// this type and the parent node's own children and (b) ensure that the parent
/// node outlives instances of this type, to avoid premature detachment from the
/// inspect tree.
///
/// Supported field-level attributes:
/// - `inspect(skip)`: Ignore this field in inspect entirely.
/// - `inspect(rename = "foo")`: Use a different name for the inspect node or
///   property of this field. By default, the field identifier is used.
/// - `inspect(forward)`: Forward attachments directly to a child field. Only
///   a single field can be forwarded, and `inspect_node` must be absent. As
///   a result the name of the field will be unused. Useful for wrapper types.
///   Note that other non-forward `Inspect` fields are allowed and their
///   semantics are unaffected.
///
/// Type- and lifetime parameters are supported if they are ignored.
#[proc_macro_derive(Inspect, attributes(inspect))]
pub fn derive_inspect(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let ast = parse_macro_input!(input as DeriveInput);
    match derive_inspect_inner(ast) {
        Ok(token_stream) => token_stream,
        Err(err) => err.to_compile_error(),
    }
    .into()
}

fn derive_inspect_inner(ast: DeriveInput) -> Result<TokenStream, Error> {
    let name = &ast.ident;
    check_container_attrs(&ast)?;
    let fields = match ast.data {
        syn::Data::Struct(syn::DataStruct {
            fields: syn::Fields::Named(syn::FieldsNamed { ref named, .. }),
            ..
        }) => Ok(named),
        _ => Err(Error::new_spanned(&ast, "can only derive Inspect on named structs")),
    }?;
    let mut inspect_fields = Vec::new();
    let mut has_inspect_node = false;
    for field in fields {
        if field.ident.as_ref().expect("internal error: expected named field") == "inspect_node" {
            has_inspect_node = true;
            continue;
        }
        if let Some(data_field) = InspectField::try_from_field(field)? {
            inspect_fields.push(data_field);
        }
    }
    let forward_count = inspect_fields.iter().filter(|f| f.attr_args.forward).count();
    if forward_count > 1 {
        return Err(Error::new_spanned(&ast, "only one inspect(forward) is allowed"));
    }
    let node_setup_stmt = if has_inspect_node {
        quote! {
            self.inspect_node = parent.create_child(name);
        }
    } else {
        TokenStream::new()
    };

    let (impl_generics, ty_generics, where_clause) = ast.generics.split_for_impl();
    let iattach_stmts = inspect_fields
        .iter()
        .map(|f| f.iattach_stmt(has_inspect_node))
        .collect::<Result<Vec<_>, _>>()?;

    Ok(quote! {

        impl #impl_generics ::fuchsia_inspect_derive::Inspect for &mut #name #ty_generics
            #where_clause
        {
            fn iattach(
                self,
                parent: &::fuchsia_inspect_derive::InspectNode,
                name: impl AsRef<str>
            ) -> std::result::Result<(), fuchsia_inspect_derive::AttachError> {
                #node_setup_stmt
                #(#iattach_stmts;)*
                Ok(())
            }
        }
    })
}
