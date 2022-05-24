// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Deref;

mod parse;

use {
    parse::*,
    proc_macro2::{Literal, Span, TokenStream, TokenTree},
    syn::{
        parse_macro_input, spanned::Spanned, Data, DataStruct, DeriveInput, Error, Expr, Fields,
        Ident, Lit, Type,
    },
    synstructure::quote,
};

/// A custom attribute for defining bit fields.
///
/// The target type must be a tuple struct with a single element which is either an unsigned integer
/// (`u8`, `u16`, `u32`, `u64` or `u128`) or a fixed-size byte array. Examples:
///
/// ```
/// #[bitfield(
///     0..=3   foo,
///     4       bar,
///     5..=31  baz,
/// )]
/// pub struct MyFields(pub u32);
///
/// #[bitfield(
///     0..=1   zip,
///     2..=12  bop,
///     13..=39  blip,
/// )]
/// pub struct MyArrayFields(pub [u8; 5]);
/// ```
///
/// A raw() getter is always generated for getting the underlying unsigned integer:
///
/// ```
/// impl MyFields {
///     pub fn raw(&self) -> u32 { self.0 }
/// ```
///
/// There are two styles of fields:
///
/// * Single-bit fields, e.g. the `bar` above. For those fields, `bool`-based accessors
///   will be generated:
///
///   ```
///   impl MyFields {
///       pub fn bar(&self) -> bool { ... }
///       pub fn set_bar(&mut self, value: bool) { ... }
///       pub fn with_bar(self, value: bool) -> Self { ... }
///   }
///   ```
///
/// * Multiple-bit fields, e.g. the `foo` field above. The bit range that defines the field
///   is inclusive (e.g., `0..=3` means bits 0 through 3, inclusive).
///   For multiple-bit fields on an underlying integer, the underlying integer type will be used:
///   For multiple-bit fields on an underlying array, an appropriate integer type will be selected:
///
///   ```
///   impl MyFields {
///       pub fn foo(&self) -> u32 { ... }
///       pub fn set_foo(&mut self, value: u32) { ... }
///       pub fn with_foo(self, value: u32) -> Self { ... }
///   }
///
///   impl MyArrayFields {
///       pub fn zip(&self) -> u8 { ... }
///       pub fn bop(&self) -> u16 { ... }
///       pub fn blip(&self) -> u32 { ... }
///   }
///   ```
///
///   Note that if a larger integer value is passed into `set_*()` or `with_*()` than the field
///   can actually contain, the value will be silently truncated to the field's width.
///   For example, passing `0x78` into `set_foo()` will set `foo` to `0x08` since it is only
///   four bits wide.
///
/// Note that different fields are not allowed to overlap, and all bits of the integer must be
/// covered. If some of the bits are actually unused, you can use `_` in place of the field name:
///
/// ```
/// #[bitfield(
///     0..=3   foo,
///     4..=6   _, // reserved
///     7       bar,
/// )]
/// pub struct MoreFields(pub u8);
/// ```
///
/// The macro also generates a `Debug` trait implementation that prints values for all
/// individual members, as well as the raw value of the entire bit field. Therefore, including
/// an additional #[derive(Debug)] will produce a compilation error because of conflicting
/// implementations.
///
///
/// Custom "Newtypes" for Fields
/// ----------------------------
/// By default, generated functions take and return values of the same integer type as
/// the underlying type of the struct. Sometimes it is desirable to use a "newtype" instead,
/// i.e. a strongly typed wrapper around an unsigned integer. For example, to represent
/// an 802.11 frame type, one can define the following "newtype":
///
/// ```
/// #[derive(Cope, Clone, Debug, PartialEq, Eq)]
/// pub struct FrameType(pub u8);
///
/// impl FrameType {
///     pub const MGMT: Self = FrameType(0);
///     pub const CTRL: Self = FrameType(1);
///     pub const DATA: Self = FrameType(2);
///     pub const EXT: Self = FrameType(3);
/// }
/// ```
///
/// Then, the `FrameControl` bitfield can use the `FrameType` struct as a type
/// for the "frame_type" field:
///
/// ```
/// #[bitfield(
///     ...
///     2..=3   frame_type as FrameType(u8),
///     ...
/// )]
/// pub struct FrameControl(pub u16);
/// ```
///
/// This will generate the following methods:
/// ```
/// impl FrameControl {
///     pub fn frame_type(&self) -> FrameType { ... }
///     pub fn set_frame_type(&mut self, value: FrameType) { ... }
///     pub fn with_frame_type(self, value: FrameType) -> Self { ... }
/// }
/// ```
///
/// In addition, regular methods will also be generated, but with a "_raw" suffix:
/// ```
/// impl FrameControl {
///     pub fn frame_type_raw(&self) -> u16 { ... }
///     pub fn set_frame_type_raw(&mut self, value: u16) { ... }
///     pub fn with_frame_type_raw(self, value: u16) -> Self { ... }
/// }
/// ```
///
/// Note that the custom type is required to implement the `Debug` trait.
///
///
/// Aliases
/// -------
/// One can specify more than one name and "newtype" for a given range of bits using the `union`
/// keyword, for example:
///
/// ```
/// #[bitfield(
///     ...
///     4..=7   union {
///                 frame_subtype,
///                 mgmt_subtype as MgmtSubtype(u8),
///                 data_subtype as DataSubtype(u8),
///             }
///     ...
/// )]
/// pub struct FrameControl(pub u16);
/// ```
///
/// Here, the names `frame_subtype`, `mgmt_subtype` and `data_subtype` refer to the same bit range
/// but have different data types. This allows interpreting the same bits differently,
/// depending on context.
///
#[proc_macro_attribute]
pub fn bitfield(
    attr: proc_macro::TokenStream,
    item: proc_macro::TokenStream,
) -> proc_macro::TokenStream {
    let mut ret = item.clone();
    let fields = parse_macro_input!(attr as FieldList);
    let input = parse_macro_input!(item as DeriveInput);

    let (impl_code, errors) = generate(&input.ident, &fields, &input);

    let errors_code: proc_macro::TokenStream =
        errors.iter().map(Error::to_compile_error).collect::<TokenStream>().into();
    let impl_code: proc_macro::TokenStream = impl_code.into();

    ret.extend(errors_code);
    ret.extend(impl_code);
    ret
}

fn generate(
    struct_name: &Ident,
    fields: &FieldList,
    struct_def: &DeriveInput,
) -> (TokenStream, Vec<Error>) {
    let mut errors = Vec::new();
    let data_format = match get_data_format_from_struct_def(struct_def) {
        Ok(format) => format,
        Err(e) => {
            errors.push(e);
            return (quote! {}, errors);
        }
    };

    check_overlaps_and_gaps(fields, data_format, &mut errors);
    check_user_types(fields, &mut errors);

    let mut methods = vec![];
    for f in &fields.fields {
        for name in f.aliases.all_aliases() {
            match generate_methods_for_alias(f, name, data_format) {
                Ok(m) => methods.push(m),
                Err(e) => errors.push(e),
            }
        }
    }
    methods.push(generate_raw_getter(data_format));

    let debug_impl = generate_debug_impl(struct_name, fields, data_format);

    let impl_code = quote! {
        impl #struct_name {
            #( #methods )*
        }
        #debug_impl
    };

    (impl_code, errors)
}

fn check_overlaps_and_gaps(fields: &FieldList, data_format: DataFormat, errors: &mut Vec<Error>) {
    let len_bits = data_format.len_bits();
    let mut used_by: Vec<Option<&FieldDef>> = vec![None; len_bits];
    for f in &fields.fields {
        let (start, end) = match &f.bits {
            BitRange::Closed { start_inclusive, end_inclusive, .. } => {
                (start_inclusive, end_inclusive)
            }
            BitRange::SingleBit { index } => (index, index),
        };
        let start_value = match start.base10_parse::<usize>() {
            Ok(v) => v,
            Err(e) => {
                errors.push(e);
                // just default to a zero value so more errors can be gathered
                0
            }
        };
        let end_value = match end.base10_parse::<usize>() {
            Ok(v) => v,
            Err(e) => {
                errors.push(e);
                // just default to a zero value so more errors can be gathered
                0
            }
        };
        if start_value >= len_bits {
            errors.push(Error::new(
                start.span(),
                format!("start index {} is out of range of {}-bit value", start_value, len_bits),
            ));
        }
        if end_value >= len_bits {
            errors.push(Error::new(
                end.span(),
                format!("end index {} is out of range of {}-bit value", end_value, len_bits),
            ));
        }
        if start_value > end_value {
            errors.push(Error::new(
                start.span(),
                format!("start index {} exceeds end index {}", start_value, end_value),
            ));
        }
        for i in start_value..std::cmp::min(end_value + 1, used_by.len()) {
            if let Some(other_field) = used_by[i] {
                errors.push(Error::new(
                    f.bits.span(),
                    format!(
                        "fields `{}` and `{}` overlap",
                        f.aliases.first_name(),
                        other_field.aliases.first_name()
                    ),
                ));
            }
            used_by[i] = Some(f);
        }
    }

    let mut gaps = vec![];
    for (i, field) in used_by.iter().enumerate() {
        if field.is_none() {
            match gaps.last_mut() {
                Some((_start, end)) if *end + 1 == i => *end = i,
                _ => gaps.push((i, i)),
            }
        }
    }

    if !gaps.is_empty() {
        let gaps_str = gaps.iter().map(gap_to_string).collect::<Vec<_>>().join(", ");
        errors.push(Error::new(
            Span::call_site(),
            format!(
                "bits {} are not covered. Please specify all unused ranges of bits \
                 with '_' as a name, e.g. `{} _,`",
                gaps_str,
                gap_to_string(&gaps[0])
            ),
        ));
    }
}

fn check_user_types(fields: &FieldList, errors: &mut Vec<Error>) {
    for field in &fields.fields {
        for alias in field.aliases.all_aliases() {
            if let Some(user_type) = &alias.user_type {
                match &field.bits {
                    BitRange::Closed { start_inclusive, end_inclusive, .. } => {
                        let start_value = match start_inclusive.base10_parse::<usize>() {
                            Ok(v) => v,
                            Err(e) => {
                                errors.push(e);
                                // just default to a zero value so more errors
                                // can be gathered
                                0
                            }
                        };
                        let end_value = match end_inclusive.base10_parse::<usize>() {
                            Ok(v) => v,
                            Err(e) => {
                                errors.push(e);
                                // just default to a zero value so more errors
                                // can be gathered
                                0
                            }
                        };

                        let field_len = if start_value <= end_value {
                            end_value - start_value + 1
                        } else {
                            continue;
                        };
                        let inner = &user_type.inner_int_type;
                        let user_type_len = match get_data_format_from_type(inner) {
                            Some(DataFormat::Integer(len)) => len,
                            _ => {
                                errors.push(Error::new(
                                    inner.span(),
                                    "expected bool, u8, u16, u32, u64 or u128",
                                ));
                                continue;
                            }
                        };
                        if user_type_len < field_len {
                            errors.push(Error::new(
                                inner.span(),
                                format!("type is too small to hold {} bits", field_len),
                            ));
                        }
                    }
                    BitRange::SingleBit { .. } => {
                        match &user_type.inner_int_type {
                            Type::Path(path) if path.path.is_ident("bool") => (),
                            other => errors.push(Error::new(other.span(), "expected `bool`")),
                        };
                    }
                };
            }
        }
    }
}

fn gap_to_string((start, end): &(usize, usize)) -> String {
    if start == end {
        format!("{}", start)
    } else {
        format!("{}..={}", start, end)
    }
}

#[derive(Debug, Copy, Clone)]
enum DataFormat {
    Integer(usize),
    ByteArrayLE(usize),
}

impl DataFormat {
    fn raw_type(&self) -> TokenStream {
        match self {
            Self::Integer(len_bits) => {
                let raw = syn::Ident::new(&format!("u{}", len_bits), Span::call_site());
                quote! { #raw }
            }
            Self::ByteArrayLE(len) => {
                quote! { [u8; #len] }
            }
        }
    }

    fn len_bits(&self) -> usize {
        match self {
            Self::Integer(len) => *len,
            Self::ByteArrayLE(len) => 8 * len,
        }
    }

    fn debug_format_string(&self) -> String {
        match self {
            Self::Integer(len) => format!("0x{{:0{}x}}", len / 4),
            Self::ByteArrayLE(_) => "{:?}".to_string(),
        }
    }

    fn int_field_functions(
        &self,
        start_inclusive: &syn::LitInt,
        end_inclusive: &syn::LitInt,
        raw_getter_fn_name: &syn::Ident,
        raw_setter_fn_name: &syn::Ident,
        raw_builder_fn_name: &syn::Ident,
    ) -> Result<(TokenStream, TokenStream), Error> {
        let start_value = start_inclusive.base10_parse::<usize>()?;
        let end_value = end_inclusive.base10_parse::<usize>()?;
        let field_len = if end_value >= start_value {
            end_value - start_value + 1
        } else {
            // If start exceeds end, we will generate a compile error.
            // Here, we still proceed with a fake value, so that calls to getters and setters
            // don't generate more compile errors.
            0
        };
        let mask = TokenTree::Literal(Literal::u128_unsuffixed(!(!0u128 << field_len)));
        match self {
            Self::Integer(len) => {
                let int_type = syn::Ident::new(&format!("u{}", len), Span::call_site());
                Ok((
                    quote! {
                        pub fn #raw_getter_fn_name(&self) -> #int_type {
                            (self.0 >> #start_inclusive) & #mask
                        }
                        pub fn #raw_setter_fn_name(&mut self, value: #int_type) {
                            self.0 = (self.0 & !(#mask << #start_inclusive))
                                   | ((value & #mask) << #start_inclusive);
                        }
                        pub fn #raw_builder_fn_name(mut self, value: #int_type) -> Self {
                            self.#raw_setter_fn_name(value);
                            self
                        }
                    },
                    quote! { #int_type },
                ))
            }
            Self::ByteArrayLE(_) => {
                let int_type = get_field_type_from_bit_len(field_len)?;
                let first_cell = start_value / 8;
                let last_cell = end_value / 8;
                let overall_offset = start_value % 8;
                Ok((
                    quote! {
                        pub fn #raw_getter_fn_name(&self) -> #int_type {
                            let mut value = 0u128;
                            for idx in (#first_cell..=#last_cell).rev() {
                                value = (value << 8) | self.0[idx] as u128;
                            }
                            ((value >> #overall_offset) & #mask) as #int_type
                        }
                        pub fn #raw_setter_fn_name(&mut self, value: #int_type) {
                            // Update each cell individually by taking u8 slices from
                            // offset_value and clear_mask using cell_mask.
                            let mut offset_value = (value as u128) << #overall_offset;
                            let mut clear_mask = !(#mask << #overall_offset);
                            let cell_mask = !(0u8) as u128;
                            for idx in #first_cell..=#last_cell {
                                self.0[idx] = (self.0[idx] & ((clear_mask & cell_mask) as u8)) |
                                    ((offset_value & cell_mask) as u8);
                                offset_value = offset_value >> 8;
                                clear_mask = clear_mask >> 8;
                            }
                        }
                        pub fn #raw_builder_fn_name(mut self, value: #int_type) -> Self {
                            self.#raw_setter_fn_name(value);
                            self
                        }
                    },
                    quote! { #int_type },
                ))
            }
        }
    }

    fn bool_field_functions(
        &self,
        index: &syn::LitInt,
        raw_getter_fn_name: &syn::Ident,
        raw_setter_fn_name: &syn::Ident,
        raw_builder_fn_name: &syn::Ident,
    ) -> Result<(TokenStream, TokenStream), Error> {
        match self {
            Self::Integer(len) => {
                let int_type = syn::Ident::new(&format!("u{}", len), Span::call_site());
                Ok((
                    quote! {
                        pub fn #raw_getter_fn_name(&self) -> bool {
                            self.0 & (1 << #index) != 0
                        }
                        pub fn #raw_setter_fn_name(&mut self, value: bool) {
                            self.0 = (self.0 & !(1 << #index)) | ((value as #int_type) << #index);
                        }
                        pub fn #raw_builder_fn_name(mut self, value: bool) -> Self {
                            self.#raw_setter_fn_name(value);
                            self
                        }
                    },
                    quote! { bool },
                ))
            }
            Self::ByteArrayLE(_) => {
                let index_value = index.base10_parse::<usize>()?;
                let cell_number = index_value / 8;
                let index_in_cell = index_value % 8;
                Ok((
                    quote! {
                        pub fn #raw_getter_fn_name(&self) -> bool {
                            self.0[#cell_number] & (1 << #index_in_cell) != 0
                        }
                        pub fn #raw_setter_fn_name(&mut self, value: bool) {
                            self.0[#cell_number] = (self.0[#cell_number] & !(1 << #index_in_cell)) |
                                ((value as u8) << #index_in_cell);
                        }
                        pub fn #raw_builder_fn_name(mut self, value: bool) -> Self {
                            self.#raw_setter_fn_name(value);
                            self
                        }
                    },
                    quote! { bool },
                ))
            }
        }
    }
}

fn get_data_format_from_struct_def(struct_def: &DeriveInput) -> Result<DataFormat, Error> {
    Ok(match &struct_def.data {
        Data::Struct(DataStruct { fields: Fields::Unnamed(fields), .. }) => {
            if fields.unnamed.len() != 1 {
                return Err(Error::new(
                    fields.paren_token.span,
                    "expected a tuple struct with a single field",
                ));
            }
            let field = &fields.unnamed.first().unwrap();
            let ty = &field.ty;
            match get_data_format_from_type(ty) {
                Some(len) => len,
                None => {
                    return Err(Error::new(
                        field.span(),
                        "expected u8, u16, u32, u64, u128 or [u8; _]",
                    ))
                }
            }
        }
        _ => {
            return Err(Error::new(
                struct_def.span(),
                "bitfield macro only supports tuple-style structs".to_string(),
            ))
        }
    })
}

fn get_bit_len_from_unsigned_type_path(path: &syn::TypePath) -> Option<usize> {
    Some(match path {
        path if path.path.is_ident("u8") => 8,
        path if path.path.is_ident("u16") => 16,
        path if path.path.is_ident("u32") => 32,
        path if path.path.is_ident("u64") => 64,
        path if path.path.is_ident("u128") => 128,
        _ => return None,
    })
}

fn get_field_type_from_bit_len(len_bits: usize) -> Result<syn::Ident, Error> {
    Ok(syn::Ident::new(
        &format!(
            "u{}",
            match len_bits {
                len_bits if len_bits <= 8 => 8,
                len_bits if len_bits <= 16 => 16,
                len_bits if len_bits <= 32 => 32,
                len_bits if len_bits <= 64 => 64,
                len_bits if len_bits <= 128 => 128,
                _ =>
                    return Err(Error::new(
                        Span::call_site(),
                        "field exceeds maximum integer size of 128",
                    )),
            }
        ),
        Span::call_site(),
    ))
}

fn get_data_format_from_type(ty: &Type) -> Option<DataFormat> {
    Some(match ty {
        Type::Path(path) => DataFormat::Integer(get_bit_len_from_unsigned_type_path(path)?),
        Type::Array(array) => {
            let len = match &array.len {
                Expr::Lit(lit) => match &lit.lit {
                    Lit::Int(len) => len.base10_parse::<usize>().ok(),
                    _ => None,
                },
                _ => None,
            }?;
            let cell_width = match array.elem.deref() {
                Type::Path(path) => get_bit_len_from_unsigned_type_path(path),
                _ => None,
            }?;
            if cell_width != 8 {
                return None;
            }
            DataFormat::ByteArrayLE(len)
        }
        _ => return None,
    })
}

fn add_suffix(ident: &Ident, suffix: &str) -> Ident {
    syn::Ident::new(&format!("{}{}", ident, suffix), Span::call_site())
}

fn generate_methods_for_alias(
    field: &FieldDef,
    alias: &Alias,
    data_format: DataFormat,
) -> Result<TokenStream, Error> {
    let getter_fn_name = &alias.name;
    let setter_fn_name = syn::Ident::new(&format!("set_{}", alias.name), alias.name.span());
    let builder_fn_name = syn::Ident::new(&format!("with_{}", alias.name), alias.name.span());
    let (raw_getter_fn_name, raw_setter_fn_name, raw_builder_fn_name) = match alias.user_type {
        None => (getter_fn_name.clone(), setter_fn_name.clone(), builder_fn_name.clone()),
        Some(_) => {
            // If a custom newtype is provided, generate regular method with a "_raw" suffix
            (
                add_suffix(getter_fn_name, "_raw"),
                add_suffix(&setter_fn_name, "_raw"),
                add_suffix(&builder_fn_name, "_raw"),
            )
        }
    };
    let (raw, raw_type) = match &field.bits {
        BitRange::Closed { start_inclusive, end_inclusive, .. } => data_format
            .int_field_functions(
                start_inclusive,
                end_inclusive,
                &raw_getter_fn_name,
                &raw_setter_fn_name,
                &raw_builder_fn_name,
            )?,
        BitRange::SingleBit { index } => data_format.bool_field_functions(
            index,
            &raw_getter_fn_name,
            &raw_setter_fn_name,
            &raw_builder_fn_name,
        )?,
    };
    let value = match &alias.user_type {
        None => raw,
        Some(user_type) => {
            let user_int_type = &user_type.inner_int_type;
            let user_type_name = &user_type.type_name;
            quote! {
                #raw

                pub fn #getter_fn_name(&self) -> #user_type_name {
                    #user_type_name(self.#raw_getter_fn_name() as #user_int_type)
                }
                pub fn #setter_fn_name(&mut self, value: #user_type_name) {
                    self.#raw_setter_fn_name(value.0 as #raw_type);
                }
                pub fn #builder_fn_name(mut self, value: #user_type_name) -> Self {
                    self.#raw_builder_fn_name(value.0 as #raw_type)
                }
            }
        }
    };
    Ok(value)
}

fn generate_raw_getter(data_format: DataFormat) -> TokenStream {
    let raw_type = data_format.raw_type();
    quote! {
        pub fn raw(&self) -> #raw_type {
            self.0
        }
    }
}

fn generate_debug_impl(
    struct_name: &Ident,
    fields: &FieldList,
    data_format: DataFormat,
) -> TokenStream {
    let mut per_alias = vec![];
    for f in fields.fields.iter() {
        for alias in f.aliases.all_aliases() {
            per_alias.push(generate_debug_for_alias(f, alias));
        }
    }
    let struct_name_str = struct_name.to_string();
    let format_string = data_format.debug_format_string();
    quote! {
        impl ::std::fmt::Debug for #struct_name {
            fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                f.debug_struct(#struct_name_str)
                    .field("0", &format_args!(#format_string, self.0))
                    #( #per_alias )*
                    .finish()
            }
        }
    }
}

fn generate_debug_for_alias(field: &FieldDef, alias: &Alias) -> TokenStream {
    let name = &alias.name;
    let name_string = name.to_string();
    let format_string = match &alias.user_type {
        None => match &field.bits {
            BitRange::Closed { .. } => "{:#x}",
            BitRange::SingleBit { .. } => "{}",
        },
        Some(_user_type) => "{:?}",
    };
    quote! {
        .field(#name_string, &format_args!(#format_string, self.#name()))
    }
}
