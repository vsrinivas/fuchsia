// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

extern crate proc_macro;

use heck::CamelCase;
use proc_macro2::{Span, TokenStream};
use quote::quote;
use std::convert::TryFrom;
use syn::spanned::Spanned;
use syn::{punctuated::*, token::*, *};

type Result<T> = std::result::Result<T, Error>;

const INVALID_FIDL_FIELD_ATTRIBUTE_MSG: &str =
    "fidl_field_type attribute must be required, optional, or default = value";

#[proc_macro_derive(
    ValidFidlTable,
    attributes(fidl_table_src, fidl_field_type, fidl_table_validator)
)]
pub fn validate_fidl_table(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let input: DeriveInput = syn::parse(input).unwrap();
    match input.data {
        Data::Struct(DataStruct { fields: syn::Fields::Named(fields), .. }) => {
            match impl_valid_fidl_table(&input.ident, fields.named, &input.attrs) {
                Ok(v) => v.into(),
                Err(e) => e.to_compile_error().into(),
            }
        }
        _ => Error::new(input.span(), "ValidateFidlTable only supports non-tuple structs!")
            .to_compile_error()
            .into(),
    }
}

fn metas<'a>(attrs: &'a [Attribute]) -> impl Iterator<Item = Meta> + 'a {
    attrs.iter().filter_map(|a| match a.parse_meta() {
        Ok(meta) => Some(meta),
        Err(_) => None,
    })
}

fn list_with_arg(meta: Meta, call: &str) -> Option<NestedMeta> {
    match meta {
        Meta::List(list) => {
            if list.path.is_ident(call) && list.nested.len() == 1 {
                Some(list.nested.into_pairs().next().unwrap().into_value())
            } else {
                None
            }
        }
        _ => None,
    }
}

fn unique_list_with_arg(attrs: &[Attribute], call: &str) -> Result<Option<NestedMeta>> {
    let metas = metas(attrs);
    let mut lists_with_arg: Vec<NestedMeta> =
        metas.filter_map(|meta| list_with_arg(meta, call)).collect();
    if lists_with_arg.len() > 1 {
        return Err(Error::new(
            lists_with_arg[1].span(),
            &format!("The {} attribute should only be declared once.", call),
        ));
    }

    Ok(lists_with_arg.pop())
}

fn fidl_table_type(span: Span, attrs: &[Attribute]) -> Result<Ident> {
    match unique_list_with_arg(attrs, "fidl_table_src")? {
        Some(nested) => match nested {
            NestedMeta::Meta(Meta::Path(fidl_table_type)) => fidl_table_type
                .get_ident()
                .cloned()
                .ok_or(Error::new(fidl_table_type.span(), "Invalid identifier")),
            _ => Err(Error::new(
                span,
                concat!(
                    "The #[fidl_table_src(FidlTableType)] attribute ",
                    "takes only one argument, a type name."
                ),
            )),
        },
        _ => Err(Error::new(
            span,
            concat!(
                "To derive ValidFidlTable, struct needs ",
                "#[fidl_table_src(FidlTableType)] attribute to mark ",
                "source Fidl type."
            ),
        )),
    }
}

fn fidl_table_validator(span: Span, attrs: &[Attribute]) -> Result<Option<Ident>> {
    unique_list_with_arg(attrs, "fidl_table_validator")?
        .map(|nested| match nested {
            NestedMeta::Meta(Meta::Path(fidl_table_validator)) => fidl_table_validator
                .get_ident()
                .cloned()
                .ok_or(Error::new(fidl_table_validator.span(), "Invalid Identifier")),
            _ => Err(Error::new(
                span,
                concat!(
                    "The #[fidl_table(FidlTableType)] attribute takes ",
                    "only one argument, a type name."
                ),
            )),
        })
        .transpose()
}

#[derive(Clone)]
struct FidlField {
    ident: Ident,
    kind: FidlFieldKind,
}

impl TryFrom<Field> for FidlField {
    type Error = Error;
    fn try_from(src: Field) -> Result<Self> {
        let span = src.span().clone();
        let attrs = src.attrs;
        let kind = FidlFieldKind::try_from(attrs.as_slice())?;
        match src.ident {
            Some(ident) => Ok(FidlField { ident, kind }),
            None => {
                Err(Error::new(span, "ValidFidlTable can only be derived for non-tuple structs."))
            }
        }
    }
}

impl FidlField {
    fn camel_case(&self) -> Ident {
        let name = self.ident.to_string().to_camel_case();
        Ident::new(&name, Span::call_site())
    }
}

#[derive(Clone)]
enum FidlFieldKind {
    Required,
    Optional,
    HasDefault(Lit),
}

impl TryFrom<&[Attribute]> for FidlFieldKind {
    type Error = Error;
    fn try_from(attrs: &[Attribute]) -> Result<Self> {
        match unique_list_with_arg(attrs, "fidl_field_type")? {
            Some(NestedMeta::Meta(Meta::Path(field_type))) => field_type
                .get_ident()
                .and_then(|i| match i.to_string().as_str() {
                    "required" => Some(FidlFieldKind::Required),
                    "optional" => Some(FidlFieldKind::Optional),
                    _ => None,
                })
                .ok_or(Error::new(field_type.span(), INVALID_FIDL_FIELD_ATTRIBUTE_MSG)),
            Some(NestedMeta::Meta(Meta::NameValue(ref default_value)))
                if default_value.path.is_ident("default") =>
            {
                Ok(FidlFieldKind::HasDefault(default_value.lit.clone()))
            }
            None => Ok(FidlFieldKind::Required),
            Some(meta) => Err(Error::new(meta.span(), INVALID_FIDL_FIELD_ATTRIBUTE_MSG)),
        }
    }
}

fn impl_valid_fidl_table(
    name: &Ident,
    fields: Punctuated<Field, Comma>,
    attrs: &[Attribute],
) -> Result<TokenStream> {
    let fidl_table_type = fidl_table_type(name.span(), attrs)?;

    let missing_field_error_type = {
        let mut error_type_name = fidl_table_type.to_string();
        error_type_name.push_str("MissingFieldError");
        Ident::new(&error_type_name, Span::call_site())
    };

    let error_type_name = {
        let mut error_type_name = fidl_table_type.to_string();
        error_type_name.push_str("ValidationError");
        Ident::new(&error_type_name, Span::call_site())
    };

    let custom_validator = fidl_table_validator(name.span(), attrs)?;
    let custom_validator_error = custom_validator
        .as_ref()
        .map(|validator| quote!(Logical(<#validator as Validate<#name>>::Error),));
    let custom_validator_call =
        custom_validator.as_ref().map(|validator| quote!(#validator::validate(&maybe_valid)?;));
    let custom_validator_error_from_impl = custom_validator.map(|validator| {
        quote!(
            impl From<<#validator as Validate<#name>>::Error> for #error_type_name {
                fn from(src: <#validator as Validate<#name>>::Error) -> Self {
                    #error_type_name::Logical(src)
                }
            }
        )
    });

    let fields: Vec<FidlField> = fields
        .into_pairs()
        .map(Pair::into_value)
        .map(FidlField::try_from)
        .collect::<Result<Vec<FidlField>>>()?;

    let mut field_validations = TokenStream::new();
    field_validations.extend(fields.iter().map(|field| {
        let ident = &field.ident;
        match &field.kind {
            FidlFieldKind::Required => {
                let camel_case = field.camel_case();
                quote!(
                    #ident: std::convert::TryFrom::try_from(
                        src.#ident.ok_or(#missing_field_error_type::#camel_case)?
                    ).map_err(|e| Box::new(e) as Box<std::error::Error + Send + Sync>)?,
                )
            }
            FidlFieldKind::Optional => quote!(
                #ident: std::convert::TryFrom::try_from(
                    src.#ident
                ).map_err(|e| Box::new(e) as Box<std::error::Error + Send + Sync>)?,
            ),
            FidlFieldKind::HasDefault(value) => quote!(
                #ident: src.#ident.unwrap_or(#value),
            ),
        }
    }));

    let mut field_intos = TokenStream::new();
    field_intos.extend(fields.iter().map(|field| {
        let ident = &field.ident;
        match &field.kind {
            FidlFieldKind::Required | FidlFieldKind::HasDefault(_) => quote!(
                #ident: Some(
                    src.#ident.into()
                ),
            ),
            FidlFieldKind::Optional => quote!(
                #ident: src.#ident.into(),
            ),
        }
    }));

    let mut field_errors = TokenStream::new();
    field_errors.extend(
        fields
            .iter()
            .filter(|field| match field.kind {
                FidlFieldKind::Required => true,
                _ => false,
            })
            .map(FidlField::camel_case)
            .map(|camel_case| quote!(#camel_case,)),
    );

    Ok(quote!(
        #[derive(Debug, Clone, Copy, PartialEq)]
        pub enum #missing_field_error_type {
            #field_errors
        }

        #[derive(Debug)]
        pub enum #error_type_name {
            MissingField(#missing_field_error_type),
            InvalidField(Box<std::error::Error + Send + Sync>),
            #custom_validator_error
        }

        impl std::fmt::Display for #error_type_name {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                write!(f, "Validation error: {:?}", self)
            }
        }

        impl std::error::Error for #error_type_name {}

        impl From<Box<std::error::Error + Send + Sync>> for #error_type_name {
            fn from(src: Box<std::error::Error + Send + Sync>) -> Self {
                #error_type_name::InvalidField(src)
            }
        }

        impl From<#missing_field_error_type> for #error_type_name {
            fn from(src: #missing_field_error_type) -> Self {
                #error_type_name::MissingField(src)
            }
        }

        #custom_validator_error_from_impl

        impl std::convert::TryFrom<#fidl_table_type> for #name {
            type Error = #error_type_name;
            fn try_from(src: #fidl_table_type) -> std::result::Result<Self, Self::Error> {
                use ::fidl_table_validation::Validate;
                let maybe_valid = Self {
                    #field_validations
                };
                #custom_validator_call
                Ok(maybe_valid)
            }
        }

        impl std::convert::From<#name> for #fidl_table_type {
            fn from(src: #name) -> #fidl_table_type {
                Self {
                    #field_intos
                }
            }
        }
    ))
}
