// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use quote::quote;

struct IdenticalEnums {
    enums: Vec<syn::Path>,
}

impl syn::parse::Parse for IdenticalEnums {
    fn parse(input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        let content;
        syn::parenthesized!(content in input);

        let mut enums = Vec::new();
        while !content.is_empty() {
            enums.push(content.parse()?);

            if content.peek(syn::token::Comma) {
                let _: syn::token::Comma = content.parse()?;
            }
        }
        Ok(IdenticalEnums { enums })
    }
}

fn generate_impls(
    item: &syn::DeriveInput,
    identical_enums: IdenticalEnums,
) -> proc_macro::TokenStream {
    let to_enum = &item.ident;
    let enum_data = match &item.data {
        syn::Data::Enum(data) => data,
        syn::Data::Struct(_) | syn::Data::Union(_) => {
            return syn::Error::new_spanned(
                item,
                "FromIdenticalEnums can only be applied to an enum type",
            )
            .to_compile_error()
            .into();
        }
    };

    let mut impls = proc_macro2::TokenStream::new();
    identical_enums.enums.iter().for_each(|from_enum| {
        let match_arms = match_arms(to_enum, from_enum, enum_data);
        impls.extend(quote! {
            impl From<#from_enum> for #to_enum {
                fn from(value: #from_enum) -> Self {
                    match value {
                        #(#match_arms)*
                    }
                }
            }
        });
    });
    proc_macro::TokenStream::from(impls)
}

fn match_arms(
    to_enum: &syn::Ident,
    from_enum: &syn::Path,
    enum_data: &syn::DataEnum,
) -> Vec<proc_macro2::TokenStream> {
    enum_data
        .variants
        .iter()
        .map(|variant| {
            let variant_name = &variant.ident;
            quote! { #from_enum::#variant_name => #to_enum::#variant_name, }
        })
        .collect()
}

/// Generates `std::convert::From` implementations for a set of identical enums.
///
/// Each generated implementation converts from a specified enum to the
/// annotated enum. One or more source enums can be specified using the
/// `identical_enums` attribute. Consider the following example:
///
/// ```
/// enum Bar {
///     A,
///     B,
/// }
///
/// #[derive(FromIdenticalEnums)]
/// #[identical_enums(Bar)]
/// enum Foo {
///     A,
///     B,
/// }
/// ```
///
/// Given the above, the following will be generated:
///
/// ```
/// impl From<Bar> for Foo {
///     fn from(value: Bar) -> Self {
///         match value {
///             Foo::A => Bar::A,
///             Foo::B => Bar::B,
///         }
///     }
/// }
/// ```
///
/// Note that all relevant enums must be identical. Enums are considered
/// identical if all variants match. Variants with associated data are not
/// supported.
#[proc_macro_derive(FromIdenticalEnums, attributes(identical_enums))]
pub fn derive_from_identical_enums(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let ast = syn::parse_macro_input!(input as syn::DeriveInput);
    let attribute = match ast
        .attrs
        .iter()
        .filter(|a| a.path.segments.len() == 1 && a.path.segments[0].ident == "identical_enums")
        .nth(0)
    {
        Some(attr) => attr,
        None => {
            return syn::Error::new_spanned(
                ast,
                "identical_enums attribute must be present when FromIdenticalEnums is used",
            )
            .to_compile_error()
            .into()
        }
    };
    let identical_enums: IdenticalEnums =
        syn::parse2(attribute.tokens.clone()).expect("invalid identical_enums attribute");
    generate_impls(&ast, identical_enums)
}
