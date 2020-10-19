// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    proc_macro2::{Ident, Span, TokenStream},
    quote::{format_ident, quote, quote_spanned},
    syn::{
        parse_macro_input, spanned::Spanned, Data, DataStruct, DeriveInput, Field, Fields, Type,
        TypePath, Visibility,
    },
};

/// A collection of the properties for translating a field in the input struct to its inspect
/// equivalent.
#[derive(Debug)]
struct InspectField {
    /// The name of the field (both in inspect and the input)
    name: Ident,
    /// The inspect `*Property` datatype.
    property: TokenStream,
    /// The inspect suffix for `create_*` and `record_*` methods.
    abbrev_type: &'static str,
    /// An expression to set the inspect property from an input struct.
    assignment: TokenStream,
    /// The span of this field in the input.
    span: Span,
}

impl From<Field> for InspectField {
    fn from(field: Field) -> InspectField {
        let span = field.span();
        let path = match field.ty {
            Type::Path(TypePath { path, .. }) => path,
            // TODO(jsankey): more elegant spanned error
            _ => panic!("InspectWritable can only be applied with fields of path type"),
        };
        let name = field.ident.expect("Could not get name of named field");
        let (property, abbrev_type, assignment) = match path.get_ident() {
            Some(ident) if (ident == "u8" || ident == "u16" || ident == "u32") => {
                (quote! {::fuchsia_inspect::UintProperty}, "uint", quote! {data.#name as u64})
            }
            Some(ident) if ident == "u64" => {
                (quote! {::fuchsia_inspect::UintProperty}, "uint", quote! {data.#name})
            }
            Some(ident) if (ident == "i8" || ident == "i16" || ident == "i32") => {
                (quote! {::fuchsia_inspect::IntProperty}, "int", quote! {data.#name as i64})
            }
            Some(ident) if ident == "i64" => {
                (quote! {::fuchsia_inspect::IntProperty}, "int", quote! {data.#name})
            }
            Some(_) | None => (
                quote! {::fuchsia_inspect::StringProperty},
                "string",
                quote! {&format!("{:?}", data.#name)},
            ),
        };
        InspectField { name, property, abbrev_type, assignment, span }
    }
}

#[proc_macro_derive(InspectWritable)]
pub fn inspect_writable_derive(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let input = parse_macro_input!(input as DeriveInput);

    let struct_name = input.ident;
    let node_name = format_ident!("{}Node", struct_name);
    match input.data {
        Data::Struct(DataStruct { fields: Fields::Named(fields), .. }) => {
            let fields = fields.named.into_iter().map(|f| InspectField::from(f)).collect();
            let struct_impl = generate_inspect_writable_impl(&struct_name, &node_name, &fields);
            let node_decl = generate_inspect_writable_node(&node_name, input.vis, &fields);
            let node_impl = generate_inspect_writable_node_impl(&struct_name, &node_name, &fields);
            return proc_macro::TokenStream::from(quote! {#struct_impl #node_decl #node_impl});
        }
        _ => panic!("InspectWritable can only be applied to structs with named fields"),
    };
}

/// Produces a token stream that implements the `InspectWritable` trait on the input struct.
fn generate_inspect_writable_impl(
    struct_name: &Ident,
    node_name: &Ident,
    fields: &Vec<InspectField>,
) -> TokenStream {
    let per_field_record =
        fields.iter().map(|InspectField { name, abbrev_type, assignment, span, .. }| {
            let method = format_ident!("record_{}", abbrev_type);
            let str_name = format!("{}", name);
            quote_spanned! {*span=> node.#method(#str_name, #assignment);}
        });

    quote! {
        impl ::inspect_writable::InspectWritable for #struct_name {
            type NodeType = #node_name;

            fn create(&self, node: Node) -> #node_name {
                <#node_name as ::inspect_writable::InspectWritableNode<Self>>::new(self, node)
            }

            fn record(&self, node: &Node) {
                let data = self;
                #(#per_field_record)*
            }
        }
    }
}

/// Produces a token stream that declares a new struct holding inspect properties.
fn generate_inspect_writable_node(
    node_name: &Ident,
    vis: Visibility,
    fields: &Vec<InspectField>,
) -> TokenStream {
    let per_field_declaration =
        fields.iter().map(|InspectField { name, property, span, .. }| {
            quote_spanned! {*span=> #name: #property}
        });
    quote! {
        #vis struct #node_name {
            node: Node,
            #(#per_field_declaration),*
        }
    }
}

/// Produces a token stream that implements the `InspectWritableNode` trait on the new struct.
fn generate_inspect_writable_node_impl(
    struct_name: &Ident,
    node_name: &Ident,
    fields: &Vec<InspectField>,
) -> TokenStream {
    let per_field_init =
        fields.iter().map(|InspectField { name, abbrev_type, assignment, span, .. }| {
            let method = format_ident!("create_{}", abbrev_type);
            let str_name = format!("{}", name);
            quote_spanned! {*span=>
                #name: ::fuchsia_inspect::Node::#method(&node, #str_name, #assignment)
            }
        });

    let per_field_update = fields.iter().map(|InspectField { name, assignment, span, .. }| {
        quote_spanned! {*span=> ::fuchsia_inspect::Property::set(&self.#name, #assignment);}
    });

    quote! {
       impl ::inspect_writable::InspectWritableNode<#struct_name> for #node_name {
            fn new(data: &#struct_name, node: Node) -> Self {
                #node_name {
                    #(#per_field_init),*,
                    node,
                }
            }

            fn update(&self,  data: &#struct_name) {
                #(#per_field_update)*
            }

            fn inspect_node(&self) -> &Node {
                &self.node
            }
        }
    }
}
