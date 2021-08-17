// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate proc_macro;

use proc_macro::TokenStream;

mod common;
mod derive_checked_vec;
mod derive_one_or_many;
mod derive_reference;

/// Macro that provides a `serde::de::Deserialize` implementation for a `Vec<T>`. Attributes
/// are provided with `#[checked_vec(...)]`.
///
/// Attributes:
/// - `expected` (required): The `expected` string attached to the serde deserializer.
/// - `min_length`: The minimum length of the vector.
/// - `unique_items`: If true, all elements of the vector must be unique. Requires `T` to
///   implement the `Hash` trait.
///
/// Example:
///
/// ```rust
/// #[derive(CheckedVec)]
/// #[checked_vec(
///     expected = "a nonempty array of rights, with unique elements",
///     min_length = 1,
///     unique_items = true,
/// )]
/// pub struct Rights(pub Vec<Right>);
/// ```
#[proc_macro_derive(CheckedVec, attributes(checked_vec))]
pub fn derive_checked_vec(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).expect("could not parse input");
    derive_checked_vec::impl_derive_checked_vec(ast)
        .unwrap_or_else(|err| err.to_compile_error())
        .into()
}

/// Macro that provides a `serde::de::Deserialize` implementation for `OneOrMany<T>`.
/// Attributes are provided with `#[one_or_many(...)]`
///
/// The type which is derived is merely a dummy type to serve as a target for the macro. The trait
/// implementation is actually on `OneOrMany<T>`.
///
/// Attributes:
/// - `expected` (required): The `expected` string attached to the serde deserializer.
/// - `inner_type` (required): The `T` of `OneOrMany<T>`.
/// - `min_length`: The minimum length of the vector.
/// - `unique_items`: If true, all elements of the vector must be unique. Requires `T` to
///   implemente the `Hash` trait.
///
/// Example:
///
/// ```rust
/// #[derive(OneOrMany)]
/// #[one_or_many(
///     expected = "a single name or a nonempty array of name, with unique elements",
///     inner_type = "Name",
///     min_length = 1,
///     unique_items = true,
/// )]
/// pub struct OneOrManyNames;
/// ```
#[proc_macro_derive(OneOrMany, attributes(one_or_many))]
pub fn derive_one_or_many(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).expect("could not parse input");
    derive_one_or_many::impl_derive_one_or_many(ast)
        .unwrap_or_else(|err| err.to_compile_error())
        .into()
}

/// Macro that provides trait implementations for a CML reference enum type that wishes to be serde
/// deserializable. This makes it possible to easily create context-specific reference types that
/// encode their accepted variants. Attributes are provided with `#[reference(...)]`.
///
/// The following enum variants are accepted:
/// - Named(Name),
/// - Parent,
/// - Framework,
/// - Debug,
/// - Self_,
///
/// Attributes:
/// - `expected` (required): The `expected` string attached to the serde deserializer.
///
/// This macro implements the following traits:
/// - `std::str::FromStr`
/// - `std::fmt::Display`
/// - `serde::de::Deserialize`
/// - `From<#this> for cml::AnyRef` (type erasure into the universal reference type)
///
/// Example:
///
/// ```rust
/// #[derive(Reference)]
/// #[reference(expected = "a registration reference")]
/// pub enum RegistrationRef {
///     /// A reference to a child.
///     Named(Name),
///     /// A reference to the parent.
///     Parent,
///     /// A reference to this component.
///     Self_,
/// }
/// ```
#[proc_macro_derive(Reference, attributes(reference))]
pub fn derive_reference(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).expect("could not parse input");
    derive_reference::impl_derive_ref(ast).unwrap_or_else(|err| err.to_compile_error()).into()
}
