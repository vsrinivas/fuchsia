// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Crate providing supporting proc-macros for the `spinel_pack` crate.
//!
//! **Don't use this crate directly**, use the `spinel_pack` crate instead.

#![doc(html_no_source)]

extern crate proc_macro;

use proc_macro_hack::proc_macro_hack;

use quote::quote;
use quote::ToTokens;
use std::str::Chars;
use syn::parse::{Parse, ParseStream};
use syn::spanned::Spanned;
use syn::{parse_macro_input, DeriveInput, GenericParam, LifetimeDef, LitStr};
use syn::{punctuated::Punctuated, Token};

const SPINEL_DATATYPE_VOID_C: char = '.';
const SPINEL_DATATYPE_BOOL_C: char = 'b';
const SPINEL_DATATYPE_UINT8_C: char = 'C';
const SPINEL_DATATYPE_INT8_C: char = 'c';
const SPINEL_DATATYPE_UINT16_C: char = 'S';
const SPINEL_DATATYPE_INT16_C: char = 's';
const SPINEL_DATATYPE_UINT32_C: char = 'L';
const SPINEL_DATATYPE_INT32_C: char = 'l';
const SPINEL_DATATYPE_UINT64_C: char = 'X';
const SPINEL_DATATYPE_INT64_C: char = 'x';
const SPINEL_DATATYPE_UINT_PACKED_C: char = 'i';
const SPINEL_DATATYPE_IPV6ADDR_C: char = '6';
const SPINEL_DATATYPE_EUI64_C: char = 'E';
const SPINEL_DATATYPE_EUI48_C: char = 'e';
const SPINEL_DATATYPE_DATA_WLEN_C: char = 'd';
const SPINEL_DATATYPE_DATA_C: char = 'D';
const SPINEL_DATATYPE_UTF8_C: char = 'U';
const SPINEL_DATATYPE_STRUCT_C: char = 't';
const SPINEL_DATATYPE_ARRAY_C: char = 'A';

/// Private method which parses the next character from a format
/// string using the given iterator, returning the actual character
/// value and a `TokenStream` for the field's Rust marker type.
///
/// If there are no more characters in the iterator, this
/// method returns `None`. If a character is encountered that
/// is invalid or not supported, a compiler error is
/// immediately raised.
///
/// This method takes a reference to a Span instance in order
/// to help provide better compiler errors.
fn get_next_format_type_info(
    format_iter: &mut Chars<'_>,
    span: &proc_macro2::Span,
) -> Option<(char, proc_macro2::TokenStream)> {
    if let Some(format) = format_iter.next() {
        let format_type_descriptor = match format {
            SPINEL_DATATYPE_VOID_C => quote! { () },
            SPINEL_DATATYPE_BOOL_C => quote! { bool },
            SPINEL_DATATYPE_UINT8_C => quote! { u8 },
            SPINEL_DATATYPE_INT8_C => quote! { i8 },
            SPINEL_DATATYPE_UINT16_C => quote! { u16 },
            SPINEL_DATATYPE_INT16_C => quote! { i16 },
            SPINEL_DATATYPE_UINT32_C => quote! { u32 },
            SPINEL_DATATYPE_INT32_C => quote! { i32 },
            SPINEL_DATATYPE_UINT64_C => quote! { u64 },
            SPINEL_DATATYPE_INT64_C => quote! { i64 },
            SPINEL_DATATYPE_IPV6ADDR_C => {
                quote! { std::net::Ipv6Addr }
            }
            SPINEL_DATATYPE_EUI64_C => {
                quote! { crate::spinel_pack::EUI64 }
            }
            SPINEL_DATATYPE_EUI48_C => {
                quote! { crate::spinel_pack::EUI48 }
            }
            SPINEL_DATATYPE_DATA_WLEN_C => quote! { crate::spinel_pack::SpinelDataWlen },
            SPINEL_DATATYPE_DATA_C => {
                if format_iter.as_str().is_empty() {
                    quote! { [u8] }
                } else {
                    let msg = format!("Invalid spinel packing format string: 'D' must be proceeded by either ')' or the end of the format, found {:?}", format_iter.as_str());
                    syn::Error::new(span.clone(), msg).to_compile_error()
                }
            }
            SPINEL_DATATYPE_UTF8_C => quote! { str },
            SPINEL_DATATYPE_UINT_PACKED_C => {
                quote! { crate::spinel_pack::SpinelUint }
            }
            SPINEL_DATATYPE_STRUCT_C => {
                let msg = "Packing format 't' is not yet implemented";
                syn::Error::new(span.clone(), msg).to_compile_error()
            }
            SPINEL_DATATYPE_ARRAY_C => {
                let msg = "Packing format 'A' is not yet implemented";
                syn::Error::new(span.clone(), msg).to_compile_error()
            }
            unknown_format => {
                let msg = format!("Unsupported field type {:?}", unknown_format);
                syn::Error::new(span.clone(), msg).to_compile_error()
            }
        };
        Some((format, format_type_descriptor))
    } else {
        None
    }
}

/// Attribute macro which takes a Spinel format string as an argument
/// and automatically defines the `TryPack`/`TryUnpack` traits for the
/// given struct.
///
/// The full list of traits implemented by this macro include:
///
/// * `TryPack`/`TryUnpack`
/// * `TryPackAs<SpinelDataWlen>`/`TryUnpackAs<SpinelDataWlen>`
/// * `TryPackAs<[u8]>`/`TryUnpackAs<[u8]>`
///
/// Additionally, if no lifetimes are specified, the following trait is
/// also implemented:
///
/// * `TryOwnedUnpack`
#[proc_macro_attribute]
pub fn spinel_packed(
    attr: proc_macro::TokenStream,
    input: proc_macro::TokenStream,
) -> proc_macro::TokenStream {
    // Parse the attributes token stream containing the format string
    // into a syn tree.
    let attr = parse_macro_input!(attr as LitStr);

    // Extract the actual format string from the attributes.
    let format: String = attr.value();

    // We keep track of the span, too, so that we can improve our error reporting.
    let format_span = attr.span();

    // Parse the input token stream with the struct definition into
    // a syn tree.
    let input = parse_macro_input!(input as DeriveInput);

    // A convenience reference to input.ident.
    let ident = &input.ident;

    // A convenience reference to input.generics.
    let generics = &input.generics;

    // A copy of the input generics without the where clause. This is
    // used for generics declaration for the implementation of the `TryPack...`
    // traits.
    let mut generics_no_where = input.generics.clone();
    generics_no_where.where_clause = None;

    // A lifetime for the input buffer for `TryUnpack`/`TryUnpackAs<>`.
    let mut buffer_lifetime = syn::Lifetime::new("'a", input.span());

    // Extract the list of fields from the struct, along with
    // the span of the last token (which we can use to improve the
    // utility of error messages)
    let (fields, last_token_span) = match input.data.clone() {
        syn::Data::Struct(syn::DataStruct {
            struct_token: _,
            fields: syn::Fields::Named(syn::FieldsNamed { brace_token: last_token, named: fields }),
            semi_token: _,
        }) => (fields, last_token.span.clone()),

        syn::Data::Struct(syn::DataStruct {
            struct_token: _,
            fields:
                syn::Fields::Unnamed(syn::FieldsUnnamed { paren_token: last_token, unnamed: fields }),
            semi_token: _,
        }) => (fields, last_token.span.clone()),

        syn::Data::Struct(syn::DataStruct {
            struct_token: syn::token::Struct { span, .. },
            fields: syn::Fields::Unit,
            semi_token: _,
        })
        | syn::Data::Enum(syn::DataEnum { enum_token: syn::token::Enum { span, .. }, .. })
        | syn::Data::Union(syn::DataUnion {
            union_token: syn::token::Union { span, .. }, ..
        }) => {
            return syn::Error::new(span, "Input must be a non-empty struct")
                .to_compile_error()
                .into()
        }
    };

    // Extract the params for the generics. Field name here is `generics_params`.
    // This will be used on all of the created trait definitions except
    // `TryOwnedUnpack`, which uses `owned_generic_params`, defined below.
    let syn::Generics { lt_token: _, params: mut generics_params, .. } = input.generics.clone();

    // This is a version of `generic_params` that is only used for the type
    // `TryOwnedUnpack`. It will be missing the buffer lifetime parameter.
    // If this ends up being equal to `None`, then `TryOwnedUnpack` will not
    // be defined.
    let owned_generic_params: Option<_>;

    // If the first generic parameter is a lifetime...
    if let Some(GenericParam::Lifetime(ref lifetime)) = generics_params.first() {
        // ...then assume it is the buffer lifetime.
        buffer_lifetime = lifetime.lifetime.clone();

        // We can't create an owned type in this case, so we set
        // `owned_generic_params` to `None`.
        owned_generic_params = None;
    } else {
        // Otherwise, we need to keep track of the original generic params
        // in `owned_generic_params` because we don't want it to have a
        // buffer lifetime.
        owned_generic_params = Some(generics_params.clone());

        // And add our buffer lifetime to `generic_params`, so that it gets
        // added to the rest of the traits.
        generics_params
            .insert(0, GenericParam::Lifetime(LifetimeDef::new(buffer_lifetime.clone())));
    }

    // A TokenStream of comma-separated field identifiers. This is filled
    // out by the while-loop below, and will be passed to the methods
    // `spinel_write` and `spinel_write_len` in order to re-use the code
    // for those macros to implement our traits.
    let mut field_idents = quote! {};

    // Contains the body of the `try_unpack()` method.
    let mut unpack_body = quote! {};

    // Iterator over the format string. Each character in the
    // format string represents a spinel type and is associated
    // with a field in the struct.
    let mut format_iter = format.chars();

    // Enumerated iterator over the struct fields. We use
    // the enumeration to identify the fields when the struct
    // is tuple-like.
    let mut field_iter = fields.iter().enumerate();

    // Iterate over all of the fields in the format string.
    while let Some((format, format_marker_type)) =
        get_next_format_type_info(&mut format_iter, &format_span)
    {
        // Extract the associated field (and field index) from the field_iter.
        let (field_index, field) = match field_iter.next() {
            Some(x) => x,
            None => {
                let msg = format!("Missing field for spinel format {:?}", format);
                return syn::Error::new(last_token_span, msg).to_compile_error().into();
            }
        };

        // Name of the unpacker trait for this field.
        let unpacker_trait = quote! { crate::spinel_pack::TryUnpackAs::<#format_marker_type> };

        // Identifier for the field, as a TokenStream. This
        // can be either a real identifier or an integer,
        // depending on if this struct is tuple-like or not.
        let ident = match field.ident.as_ref() {
            // Struct is NOT tuple-like
            Some(ident) => ident.to_token_stream(),

            // Struct IS tuple-like
            None => syn::LitInt::new(&field_index.to_string(), field.span()).to_token_stream(),
        };

        // Append the field identifier to `field_idents`.
        field_idents.extend(quote! { self.#ident, });

        // Go ahead and add the field to `unpack_body`, too.
        // Here, `unpack_body` will be eventually expanded into
        // a struct constructor for our type.
        unpack_body.extend(quote! {
            #ident: #unpacker_trait::try_unpack_as(iter)?,
        });
    }

    // Make sure that we have reached the end of the field
    // iterator. If we haven't, that means that we have a field
    // that doesn't have a corresponding spinel type in the
    // format string, which is a compile-time error.
    if let Some((_, field)) = field_iter.next() {
        return syn::Error::new(
            field.span(),
            "Struct field does not have a corresponding spinel type in format string",
        )
        .to_compile_error()
        .into();
    }

    // Contains the body of `try_pack()`, to which we defer
    // to the `spinel_write` macro implementation (which
    // we are just calling as a function here).
    let pack_body: proc_macro2::TokenStream =
        spinel_write(quote! {buffer, #format, #field_idents}.into()).into();

    // Contains the body of `try_pack_len()`, to which we defer
    // to the `spinel_write_len` macro implementation (which
    // we are just calling as a function here).
    let pack_len_body: proc_macro2::TokenStream =
        spinel_write_len(quote! {#format, #field_idents}.into()).into();

    // Calculate the body of most of the trait implementations.
    let mut output = quote! {
        #input

        impl #generics_no_where crate::spinel_pack::TryPackAs<[u8]> for #ident #generics {
            fn pack_as_len(&self) -> std::io::Result<usize> {
                #pack_len_body
            }

            fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
                #pack_body
            }
        }

        impl <#generics_params> crate::spinel_pack::TryUnpackAs<#buffer_lifetime, [u8]> for #ident #generics {
            fn try_unpack_as(iter: &mut std::slice::Iter<#buffer_lifetime, u8>) -> anyhow::Result<Self> {
                Ok(Self {
                    #unpack_body
                })
            }
        }

        // This implementation mostly defers to `TryPackAs::<[u8]>`, but
        // with some extra code to handle the prepended length.
        impl #generics_no_where crate::spinel_pack::TryPackAs<crate::spinel_pack::SpinelDataWlen> for #ident #generics {
            fn pack_as_len(&self) -> std::io::Result<usize> {
                use crate::spinel_pack::{TryPackAs};

                // The length is always the length from `TryPackAs::<[u8]>`
                // plus 2 for the prepended length.
                Ok(TryPackAs::<[u8]>::pack_as_len(self)? + 2)
            }

            fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
                use crate::spinel_pack::{TryPackAs};

                // Start with the length of the encoding
                let mut len = TryPackAs::<[u8]>::pack_as_len(self)?;

                // Encode the length of the buffer and add the
                // length of that to our total length.
                len += TryPackAs::<u16>::try_pack_as(&(len as u16), buffer)?;

                // Encode the rest of the object into the buffer.
                TryPackAs::<[u8]>::try_pack_as(self, buffer)?;

                Ok(len)
            }
        }

        // This implementation mostly defers to `TryUnpackAs::<[u8]>`, but
        // with some extra code to handle the prepended length.
        impl <#generics_params> crate::spinel_pack::TryUnpackAs<#buffer_lifetime, crate::spinel_pack::SpinelDataWlen> for #ident #generics {
            fn try_unpack_as(iter: &mut std::slice::Iter<#buffer_lifetime, u8>) -> anyhow::Result<Self> {
                use crate::spinel_pack::{TryUnpack,TryUnpackAs,SpinelDataWlen};

                // Get a reference to the buffer.
                let buffer: &#buffer_lifetime[u8] = TryUnpackAs::<SpinelDataWlen>::try_unpack_as(iter)?;

                // Unpack using that buffer.
                TryUnpackAs::<[u8]>::try_unpack_as(&mut buffer.iter())
            }
        }

        // This implementation entirely defers to `TryPackAs::<[u8]>`.
        impl #generics_no_where crate::spinel_pack::TryPack for #ident #generics {
            fn pack_len(&self) -> std::io::Result<usize> {
                crate::spinel_pack::TryPackAs::<[u8]>::pack_as_len(self)
            }

            fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
                crate::spinel_pack::TryPackAs::<[u8]>::try_pack_as(self, buffer)
            }

            fn array_pack_len(&self) -> std::io::Result<usize> {
                crate::spinel_pack::TryPackAs::<crate::spinel_pack::SpinelDataWlen>::pack_as_len(self)
            }

            fn try_array_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
                crate::spinel_pack::TryPackAs::<crate::spinel_pack::SpinelDataWlen>::try_pack_as(self, buffer)
            }
        }

        // This implementation entirely defers to `TryUnpackAs::<[u8]>`.
        impl <#generics_params> crate::spinel_pack::TryUnpack<#buffer_lifetime> for #ident #generics {
            type Unpacked = Self;
            fn try_unpack(iter: &mut std::slice::Iter<#buffer_lifetime, u8>) -> anyhow::Result<Self::Unpacked> {
                crate::spinel_pack::TryUnpackAs::<[u8]>::try_unpack_as(iter)
            }
            fn try_array_unpack(iter: &mut std::slice::Iter<#buffer_lifetime, u8>) -> anyhow::Result<Self::Unpacked> {
                use crate::spinel_pack::{TryUnpackAs, SpinelDataWlen};
                let data: &[u8] = TryUnpackAs::<SpinelDataWlen>::try_unpack_as(iter)?;
                Self::try_unpack_from_slice(data)
            }
        }
    };

    // Also include a definition for TryOwnedUnpack, if possible.
    if let Some(generics_params) = owned_generic_params {
        output.extend(quote! {
            impl <#generics_params> crate::spinel_pack::TryOwnedUnpack for #ident #generics {
                type Unpacked = Self;
                fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
                    Ok(Self {
                        #unpack_body
                    })
                }
                fn try_array_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
                    use crate::spinel_pack::{TryUnpackAs, SpinelDataWlen};
                    let data: &[u8] = TryUnpackAs::<SpinelDataWlen>::try_unpack_as(iter)?;
                    Self::try_owned_unpack_from_slice(data)
                }
            }
        });
    }

    output.into()
}

// `syn`-style datastructure for the input to the `spinel_write`.
struct PackInput {
    pub writer: syn::Expr,
    pub comma_token1: Token![,],
    pub format: syn::LitStr,
    pub comma_token2: Token![,],
    pub args: Punctuated<syn::Expr, Token![,]>,
}

impl Parse for PackInput {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        Ok(PackInput {
            writer: input.parse()?,
            comma_token1: input.parse()?,
            format: input.parse()?,
            comma_token2: input.parse()?,
            args: Punctuated::parse_terminated(input)?,
        })
    }
}

// In-line proc macro for writing spinel-formatted data fields to a type
// implementing `std::io::Write`.
#[proc_macro_hack]
pub fn spinel_write(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let input = parse_macro_input!(input as PackInput);

    let format: String = input.format.value();
    let format_span = input.format.span();

    // Iterator over all of the field types from the format string.
    let mut format_iter = format.chars();

    // Iterator over all of the field arguments.
    let mut arg_iter = input.args.iter();

    // Contains the body of commands which writes out the fields
    // to the writer.
    let mut pack_body = quote! {};

    // Iterate through each field in the format string.
    while let Some((format, format_marker_type)) =
        get_next_format_type_info(&mut format_iter, &format_span)
    {
        // Name of the packer trait for this field.
        let packer_trait = quote! { crate::spinel_pack::TryPackAs::<#format_marker_type> };

        // Get the next argument.
        if let Some(arg) = arg_iter.next() {
            // Add the field packing to the body.
            pack_body.extend(quote! {
                len += #packer_trait::try_pack_as(&(#arg),buffer)?;
            });
        } else {
            // Missing next argument, this is a compile-time error.
            let msg = format!("Missing argument for spinel format {:?}", format);
            return syn::Error::new(input.format.span(), msg).to_compile_error().into();
        }
    }

    // The `writer` needs to be in a single token in order to work
    // with the `quote!{}` macro below.
    let writer = input.writer;

    let output = quote! {
        {
            let __x: std::io::Result<usize> = (||{
                let mut len = 0;
                let mut buffer = #writer;
                #pack_body
                Ok(len)
            })();
            __x
        }
    };

    output.into()
}

// `syn`-style datastructure for the input to the `spinel_write_len`.
struct PackLenInput {
    pub format: syn::LitStr,
    pub comma_token: Token![,],
    pub args: Punctuated<syn::Expr, Token![,]>,
}

impl Parse for PackLenInput {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        Ok(PackLenInput {
            format: input.parse()?,
            comma_token: input.parse()?,
            args: Punctuated::parse_terminated(input)?,
        })
    }
}

// In-line proc macro for calculating the encoded length of specific
// spinel-formatted data field values.
#[proc_macro_hack]
pub fn spinel_write_len(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let input = parse_macro_input!(input as PackLenInput);

    let format: String = input.format.value();
    let format_span = input.format.span();

    // Iterator over all of the field types from the format string.
    let mut format_iter = format.chars();

    // Iterator over all of the field arguments.
    let mut arg_iter = input.args.iter();

    // Contains the body of commands which sum up all of the individual
    // field lengths.
    let mut pack_body = quote! {};

    // Iterate through each field in the format string.
    while let Some((format, format_marker_type)) =
        get_next_format_type_info(&mut format_iter, &format_span)
    {
        // Name of the packer trait for this field.
        let packer_trait = quote! { crate::spinel_pack::TryPackAs::<#format_marker_type> };

        // Get the next argument.
        if let Some(arg) = arg_iter.next() {
            // Add the field length calculation to the body.
            pack_body.extend(quote! {
                len += #packer_trait::pack_as_len(&(#arg))?;
            });
        } else {
            // Missing next argument, this is a compile-time error.
            let msg = format!("Missing argument for spinel format {:?}", format);
            return syn::Error::new(input.format.span(), msg).to_compile_error().into();
        }
    }

    let output = quote! {
        {
            let __x: std::io::Result<usize> = (||{
                let mut len = 0;
                #pack_body
                Ok(len)
            })();
            __x
        }
    };

    output.into()
}

// `syn`-style datastructure for the input to the `spinel_read`.
struct UnpackInput {
    pub reader: syn::Expr,
    pub comma_token1: Token![,],
    pub format: syn::LitStr,
    pub comma_token2: Token![,],
    pub args: Punctuated<syn::Expr, Token![,]>,
}

impl Parse for UnpackInput {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        Ok(UnpackInput {
            reader: input.parse()?,
            comma_token1: input.parse()?,
            format: input.parse()?,
            comma_token2: input.parse()?,
            args: Punctuated::parse_terminated(input)?,
        })
    }
}

// In-line proc macro for parsing spinel-formatted data fields from
// a byte slice iterator.
#[proc_macro_hack]
pub fn spinel_read(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let input = parse_macro_input!(input as UnpackInput);

    let format: String = input.format.value();
    let format_span = input.format.span();

    // Iterator over all of the field types from the format string.
    let mut format_iter = format.chars();

    // Iterator over all of the field arguments.
    let mut arg_iter = input.args.iter();

    // Contains the body of commands which unpack all of the fields.
    let mut unpack_body = quote! {};

    // Iterate through each field in the format string.
    while let Some((format, format_marker_type)) =
        get_next_format_type_info(&mut format_iter, &format_span)
    {
        // Name of the packer trait for this field.
        let unpacker_trait = quote! { crate::spinel_pack::TryUnpackAs::<#format_marker_type> };

        // Get the next argument.
        if let Some(arg) = arg_iter.next() {
            // Add the field unpacking to the body.
            unpack_body.extend(quote! {
                #arg = #unpacker_trait::try_unpack_as(iter)?;
            });
        } else {
            // Missing next argument, this is a compile-time error.
            let msg = format!("Missing argument for spinel format {:?}", format);
            return syn::Error::new(input.format.span(), msg).to_compile_error().into();
        }
    }

    // The `reader` needs to be in a single token in order to work
    // with the `quote!{}` macro below.
    let reader = input.reader;

    let output = quote! {
        {
            let __x: anyhow::Result<()> = (||{
                let iter: &mut std::slice::Iter<u8> = #reader;
                #unpack_body
                Ok(())
            })();
            __x
        }
    };

    output.into()
}
