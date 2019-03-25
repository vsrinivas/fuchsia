// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

mod parse;

use {
    parse::*,
    proc_macro2::{Literal, Span, TokenStream, TokenTree},
    syn::{
        parse_macro_input, spanned::Spanned, Data, DataStruct, DeriveInput, Error, Fields, Ident,
        Type,
    },
    synstructure::quote,
};

/// A custom attribute for defining bit fields.
///
/// The target type must be a tuple struct with a single element which is an unsigned integer
/// (`u8`, `u16`, `u32`, `u64` or `u128`.) Example:
///
/// ```
/// #[bitfield(
///     0..=3   foo,
///     4       bar,
///     5..=31  baz,
/// )]
/// pub struct MyFields(pub u32);
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
///   For multiple-bit fields, the underlying integer type will be used:
///
///   ```
///   impl MyFields {
///       pub fn foo(&self) -> u32 { ... }
///       pub fn set_foo(&mut self, value: u32) { ... }
///       pub fn with_foo(self, value: u32) -> Self { ... }
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
    let len_bits = match get_underlying_bit_len(struct_def) {
        Ok(len) => len,
        Err(e) => {
            errors.push(e);
            return (quote! {}, errors);
        }
    };

    check_overlaps_and_gaps(fields, len_bits, &mut errors);

    let mut methods = vec![];
    for f in &fields.fields {
        if let IdentOrUnderscore::Ident(name) = &f.name {
            methods.push(generate_methods_for_field(f, name, len_bits));
        }
    }

    let impl_code = quote! {
        impl #struct_name {
            #( #methods )*
        }
    };

    (impl_code, errors)
}

fn check_overlaps_and_gaps(fields: &FieldList, len_bits: usize, errors: &mut Vec<Error>) {
    let mut used_by: Vec<Option<&FieldDef>> = vec![None; len_bits];
    for f in &fields.fields {
        let (start, end) = match &f.bits {
            BitRange::Closed { start_inclusive, end_inclusive, .. } => {
                (start_inclusive, end_inclusive)
            }
            BitRange::SingleBit { index } => (index, index),
        };
        if start.value() as usize >= len_bits {
            errors.push(Error::new(
                start.span(),
                format!("start index {} is out of range of {}-bit value", start.value(), len_bits),
            ));
        }
        if end.value() as usize >= len_bits {
            errors.push(Error::new(
                end.span(),
                format!("end index {} is out of range of {}-bit value", end.value(), len_bits),
            ));
        }
        if start.value() > end.value() {
            errors.push(Error::new(
                start.span(),
                format!("start index {} exceeds end index {}", start.value(), end.value()),
            ));
        }
        for i in (start.value() as usize)..std::cmp::min(end.value() as usize + 1, used_by.len()) {
            if let Some(other_field) = used_by[i] {
                errors.push(Error::new(
                    f.bits.span(),
                    format!("fields `{}` and `{}` overlap", f.name, other_field.name),
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

fn gap_to_string((start, end): &(usize, usize)) -> String {
    if start == end {
        format!("{}", start)
    } else {
        format!("{}..={}", start, end)
    }
}

fn get_underlying_bit_len(struct_def: &DeriveInput) -> Result<usize, Error> {
    Ok(match &struct_def.data {
        Data::Struct(DataStruct { fields: Fields::Unnamed(fields), .. }) => {
            if fields.unnamed.len() != 1 {
                return Err(Error::new(
                    fields.paren_token.span,
                    "expected a tuple struct with a single field",
                ));
            }
            match &fields.unnamed.first().unwrap().value().ty {
                Type::Path(path) if path.path.is_ident("u8") => 8,
                Type::Path(path) if path.path.is_ident("u16") => 16,
                Type::Path(path) if path.path.is_ident("u32") => 32,
                Type::Path(path) if path.path.is_ident("u64") => 64,
                Type::Path(path) if path.path.is_ident("u128") => 128,
                other => {
                    return Err(Error::new(other.span(), "expected u8, u16, u32, u64 or u128"))
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

fn generate_methods_for_field(field: &FieldDef, name: &Ident, len_bits: usize) -> TokenStream {
    let int_type = syn::Ident::new(&format!("u{}", len_bits), Span::call_site());
    let getter_fn_name = name;
    let setter_fn_name = syn::Ident::new(&format!("set_{}", name), name.span());
    let builder_fn_name = syn::Ident::new(&format!("with_{}", name), name.span());
    match &field.bits {
        BitRange::Closed { start_inclusive, end_inclusive, .. } => {
            let len = if end_inclusive.value() >= start_inclusive.value() {
                end_inclusive.value() - start_inclusive.value() + 1
            } else {
                // If start exceeds end, we will generate a compile error.
                // Here, we still proceed with a fake value, so that calls to getters and setters
                // don't generate more compile errors.
                0
            };
            let mask = TokenTree::Literal(Literal::u128_unsuffixed(!(!0u128 << len)));
            quote! {
                pub fn #getter_fn_name(&self) -> #int_type {
                    (self.0 >> #start_inclusive) & #mask
                }
                pub fn #setter_fn_name(&mut self, value: #int_type) {
                    self.0 = (self.0 & !(#mask << #start_inclusive))
                           | ((value & #mask) << #start_inclusive);
                }
                pub fn #builder_fn_name(mut self, value: #int_type) -> Self {
                    self.#setter_fn_name(value);
                    self
                }
            }
        }
        BitRange::SingleBit { index } => {
            quote! {
                pub fn #getter_fn_name(&self) -> bool {
                    self.0 & (1 << #index) != 0
                }
                pub fn #setter_fn_name(&mut self, value: bool) {
                    self.0 = (self.0 & !(1 << #index)) | ((value as #int_type) << #index);
                }
                pub fn #builder_fn_name(mut self, value: bool) -> Self {
                    self.#setter_fn_name(value);
                    self
                }
            }
        }
    }
}
