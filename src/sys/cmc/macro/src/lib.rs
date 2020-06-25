// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    maplit::hashset,
    proc_macro::TokenStream,
    proc_macro2::{Ident, TokenStream as TokenStream2},
    quote::{quote, ToTokens, TokenStreamExt},
    std::collections::HashSet,
    syn,
};

/// Macro that provides a `serde::de::Deserialize` implementation for a `Vec<T>`. Attributes
/// may be specified to add assertions.
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
/// #[expected = "a nonempty array of rights, with unique elements"]
/// #[min_length = 1]
/// #[unique_items = true]
/// pub struct Rights(pub Vec<Right>);
/// ```
#[proc_macro_derive(CheckedVec, attributes(expected, min_length, unique_items))]
pub fn derive_checked_vec(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).expect("could not parse input");
    impl_derive_checked_vec(ast).unwrap_or_else(|err| err.to_compile_error()).into()
}

fn impl_derive_checked_vec(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let attrs = parse_checked_vec_attributes(&ast)?;

    struct Deserialize<'a> {
        ty: &'a syn::Path,
        inner_type: &'a syn::Path,
        expected: &'a syn::LitStr,
        min_length: Option<usize>,
        unique_items: bool,
    }
    impl ToTokens for Deserialize<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let ty = self.ty;
            let inner_type = self.inner_type;
            let expected = self.expected;
            let min_length = self.min_length;
            let unique_items = self.unique_items;
            let visit_seq =
                gen_visit_seq(quote!(#ty), inner_type, expected, min_length, unique_items);
            tokens.append_all(quote! {
                impl<'de> serde::de::Deserialize<'de> for #ty {
                    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
                    where
                        D: serde::de::Deserializer<'de>,
                    {
                        struct Visitor;
                        impl<'de> serde::de::Visitor<'de> for Visitor {
                            type Value = #ty;

                            fn expecting(
                                &self,
                                formatter: &mut fmt::Formatter<'_>
                            ) -> fmt::Result {
                                formatter.write_str(#expected)
                            }

                            #visit_seq
                        }
                        deserializer.deserialize_seq(Visitor)
                    }
                }
            });
        }
    }
    let deserialize = Deserialize {
        ty: &attrs.ty,
        inner_type: &attrs.inner_type,
        expected: &attrs.expected,
        min_length: attrs.min_length,
        unique_items: attrs.unique_items,
    };
    let tokens = quote! {
        #deserialize
    };
    Ok(tokens)
}

fn gen_visit_str(ty: Option<TokenStream2>, expected: &syn::LitStr) -> TokenStream2 {
    let ret = match ty {
        Some(ty) => quote!(#ty(value)),
        None => quote!(value),
    };
    quote! {
        fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            let value = value
                .parse()
                .map_err(|e| match e {
                    ParseError::InvalidValue => {
                        E::invalid_value(serde::de::Unexpected::Str(value), &#expected)
                    }
                    ParseError::InvalidLength => {
                        E::invalid_length(value.len(), &#expected)
                    }
                })?;
            Ok(#ret)
        }
    }
}

fn gen_visit_seq(
    ty: TokenStream2,
    inner_type: &syn::Path,
    expected: &syn::LitStr,
    min_length: Option<usize>,
    unique_items: bool,
) -> TokenStream2 {
    let inner = {
        let mut tokens = quote!();
        tokens.append_all(quote! {
            let mut elements = vec![];
            while let Some(e) = seq.next_element::<#inner_type>()? {
                elements.push(e);
            }
        });
        if let Some(min_length) = min_length {
            tokens.append_all(quote! {
                if elements.len() < #min_length {
                    return Err(serde::de::Error::invalid_length(elements.len(), &#expected));
                }
            });
        }
        if unique_items {
            tokens.append_all(quote! {
                let mut items = std::collections::HashSet::new();
                for e in &elements {
                    if !items.insert(e) {
                        return Err(serde::de::Error::invalid_value(
                            serde::de::Unexpected::Other(
                                "array with duplicate element"),
                            &#expected)
                        );
                    }
                }
                Ok(#ty(elements))
            });
        } else {
            tokens.append_all(quote! {
                Ok(#ty(elements))
            });
        }
        tokens
    };
    let mut tokens = quote!();
    tokens.append_all(quote! {
        fn visit_seq<A>(self, mut seq: A) -> Result<Self::Value, A::Error>
        where
            A: serde::de::SeqAccess<'de>
        {
            #inner
        }
    });
    tokens
}

/// Attributes extracted from the `derive(CheckedVec)` macro.
struct CheckedVecAttributes {
    /// Type of the struct.
    ty: syn::Path,
    /// Type inside the Vec.
    inner_type: syn::Path,
    /// `expecting` string to return from the deserializer.
    expected: syn::LitStr,
    /// The minimum length of the vector, if any.
    min_length: Option<usize>,
    /// Whether all items in the vector must be unique.
    unique_items: bool,
}

fn parse_checked_vec_attributes(
    ast: &syn::DeriveInput,
) -> Result<CheckedVecAttributes, syn::Error> {
    let inner_type;
    match &ast.data {
        syn::Data::Struct(syn::DataStruct { fields: syn::Fields::Unnamed(fields), .. }) => {
            inner_type = get_vec_inner_type(&fields).map_err(|_| {
                syn::Error::new_spanned(
                    ast,
                    "CheckedVec must be derived on a struct with one unnamed Vec field",
                )
            })?;
        }
        _ => {
            return Err(syn::Error::new_spanned(
                ast,
                "CheckedVec must be derived on a struct with one unnamed Vec field",
            ));
        }
    }
    let mut expected = None;
    let mut min_length = None;
    let mut unique_items = None;
    for attr in &ast.attrs {
        if attr.path.is_ident("expected") {
            extract_expected(ast, attr, &mut expected)?;
        } else if attr.path.is_ident("min_length") {
            extract_min_length(ast, attr, &mut min_length)?;
        } else if attr.path.is_ident("unique_items") {
            extract_unique_items(ast, attr, &mut unique_items)?;
        }
    }

    let ty = ast.ident.clone().into();
    let expected =
        expected.ok_or_else(|| syn::Error::new_spanned(ast, "`expected` attribute is missing"))?;
    let unique_items = unique_items.unwrap_or(false);

    Ok(CheckedVecAttributes { ty, inner_type, expected, min_length, unique_items })
}

#[derive(Debug)]
enum ParseError {
    InvalidAttributes,
}

fn get_vec_inner_type(fields: &syn::FieldsUnnamed) -> Result<syn::Path, ParseError> {
    if fields.unnamed.len() != 1 {
        return Err(ParseError::InvalidAttributes);
    }
    let field = fields.unnamed.first().unwrap();
    match &field.ty {
        syn::Type::Path(ty) => {
            if ty.path.segments.len() != 1 {
                return Err(ParseError::InvalidAttributes);
            }
            let seg = &ty.path.segments.first().unwrap();
            if &seg.ident.to_string() != "Vec" {
                return Err(ParseError::InvalidAttributes);
            }
            match &seg.arguments {
                syn::PathArguments::AngleBracketed(a) => {
                    if a.args.len() != 1 {
                        return Err(ParseError::InvalidAttributes);
                    }
                    match a.args.first().unwrap() {
                        syn::GenericArgument::Type(syn::Type::Path(ty)) => {
                            return Ok(ty.path.clone());
                        }
                        _ => {}
                    }
                }
                _ => {}
            }
        }
        _ => {}
    }
    Err(ParseError::InvalidAttributes)
}

/// Macro that provides a `serde::de::Deserialize` implementation for `OneOrMany<T>`.
/// Attributes may be specified to add assertions.
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
/// #[inner_type(Name)]
/// #[expected = "a single name or a nonempty array of name, with unique elements"]
/// #[min_length = 1]
/// #[unique_items = true]
/// pub struct OneOrManyNames;
/// ```
#[proc_macro_derive(OneOrMany, attributes(inner_type, expected, min_length, unique_items))]
pub fn derive_one_or_many(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).expect("could not parse input");
    impl_derive_one_or_many(ast).unwrap_or_else(|err| err.to_compile_error()).into()
}

fn impl_derive_one_or_many(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let attrs = parse_one_or_many_attributes(&ast)?;

    struct Deserialize<'a> {
        inner_type: &'a syn::Path,
        expected: &'a syn::LitStr,
        min_length: Option<usize>,
        unique_items: bool,
    }
    impl ToTokens for Deserialize<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let inner_type = self.inner_type;
            let expected = self.expected;
            let min_length = self.min_length;
            let unique_items = self.unique_items;
            let visit_str = gen_visit_str(Some(quote!(OneOrMany::One)), expected);
            let visit_seq = gen_visit_seq(
                quote!(OneOrMany::Many),
                inner_type,
                expected,
                min_length,
                unique_items,
            );
            tokens.append_all(quote! {
                impl<'de> serde::de::Deserialize<'de> for OneOrMany<#inner_type> {
                    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
                    where
                        D: serde::de::Deserializer<'de>,
                    {
                        struct Visitor;
                        impl<'de> serde::de::Visitor<'de> for Visitor {
                            type Value = OneOrMany<#inner_type>;

                            fn expecting(
                                &self,
                                formatter: &mut fmt::Formatter<'_>
                            ) -> fmt::Result {
                                formatter.write_str(#expected)
                            }

                            #visit_str

                            #visit_seq
                        }
                        deserializer.deserialize_any(Visitor)
                    }
                }
            });
        }
    }
    let deserialize = Deserialize {
        inner_type: &attrs.inner_type,
        expected: &attrs.expected,
        min_length: attrs.min_length,
        unique_items: attrs.unique_items,
    };
    let tokens = quote! {
        #deserialize
    };
    Ok(tokens)
}

/// Attributes extracted from the `derive(OneOrMany)` macro.
struct OneOrManyAttributes {
    /// Type inside the `OneOrMany`.
    inner_type: syn::Path,
    /// `expecting` string to return from the deserializer.
    expected: syn::LitStr,
    /// The minimum length of the vector, if any.
    min_length: Option<usize>,
    /// Whether all items in the vector must be unique.
    unique_items: bool,
}

fn parse_one_or_many_attributes(ast: &syn::DeriveInput) -> Result<OneOrManyAttributes, syn::Error> {
    match ast.data {
        syn::Data::Struct(syn::DataStruct { fields: syn::Fields::Unit, .. }) => {}
        _ => {
            return Err(syn::Error::new_spanned(
                ast,
                "OneOrMany must be derived on a struct with no fields",
            ));
        }
    }
    let mut expected = None;
    let mut inner_type = None;
    let mut min_length = None;
    let mut unique_items = None;
    for attr in &ast.attrs {
        if attr.path.is_ident("expected") {
            extract_expected(ast, attr, &mut expected)?;
        } else if attr.path.is_ident("inner_type") {
            extract_inner_type(ast, attr, &mut inner_type)?;
        } else if attr.path.is_ident("min_length") {
            extract_min_length(ast, attr, &mut min_length)?;
        } else if attr.path.is_ident("unique_items") {
            extract_unique_items(ast, attr, &mut unique_items)?;
        }
    }

    let inner_type = inner_type
        .ok_or_else(|| syn::Error::new_spanned(ast, "`inner_type` attribute is missing"))?;
    let expected =
        expected.ok_or_else(|| syn::Error::new_spanned(ast, "`expected` attribute is missing"))?;
    let unique_items = unique_items.unwrap_or(false);
    Ok(OneOrManyAttributes { inner_type, expected, min_length, unique_items })
}

/// Macro that provides trait implementations for a CML reference enum type that wishes to be serde
/// deserializable. This makes it possible to easily create context-specific reference types that
/// encode their accepted variants.
///
/// The following enum variants are accepted:
/// - Named(Name),
/// - Realm,
/// - Framework,
/// - Self_,
///
/// Attributes (all required):
/// - `expected`: The `expected` string attached to the serde deserializer.
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
/// #[derive(Ref)]
/// #[expected = "a registration reference"]
/// pub enum RegistrationRef {
///     /// A reference to a child.
///     Named(Name),
///     /// A reference to the containing realm.
///     Realm,
///     /// A reference to this component.
///     Self_,
/// }
/// ```
#[proc_macro_derive(Ref, attributes(expected))]
pub fn derive_ref(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).expect("could not parse input");
    impl_derive_ref(ast).unwrap_or_else(|err| err.to_compile_error()).into()
}

fn impl_derive_ref(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let attrs = parse_ref_attributes(&ast)?;

    struct Display<'a> {
        name: &'a Ident,
        variants: &'a HashSet<String>,
    }
    impl ToTokens for Display<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let name = self.name;
            let inner = {
                let mut tokens = quote!();
                if self.variants.contains("Named") {
                    tokens.append_all(quote! {
                        Self::Named(name) => write!(f, "#{}", name),
                    });
                }
                if self.variants.contains("Realm") {
                    tokens.append_all(quote! {
                        Self::Realm => write!(f, "realm"),
                    });
                }
                if self.variants.contains("Framework") {
                    tokens.append_all(quote! {
                        Self::Framework => write!(f, "framework"),
                    });
                }
                if self.variants.contains("Self_") {
                    tokens.append_all(quote! {
                        Self::Self_ => write!(f, "self"),
                    });
                }
                tokens
            };
            tokens.append_all(quote! {
                impl<'a> std::fmt::Display for #name {
                    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                        match self {
                            #inner
                        }
                    }
                }
            });
        }
    }

    struct FromStr<'a> {
        name: &'a Ident,
        variants: &'a HashSet<String>,
    }
    impl ToTokens for FromStr<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let name = self.name;
            let inner = {
                let mut tokens = quote!();
                tokens.append_all(quote! {
                    if value.len() == 0 || value.len() > 101 {
                        return Err(ParseError::InvalidLength);
                    }
                });
                if self.variants.contains("Named") {
                    tokens.append_all(quote! {
                        if value.starts_with("#") {
                            return value[1..]
                                .parse::<Name>()
                                .map(Self::Named)
                                .map_err(|_| ParseError::InvalidValue);
                        }
                    });
                }
                let inner = {
                    let mut tokens = quote!();
                    if self.variants.contains("Realm") {
                        tokens.append_all(quote! {
                            "realm" => Ok(Self::Realm),
                        });
                    }
                    if self.variants.contains("Framework") {
                        tokens.append_all(quote! {
                            "framework" => Ok(Self::Framework),
                        });
                    }
                    if self.variants.contains("Self_") {
                        tokens.append_all(quote! {
                            "self" => Ok(Self::Self_),
                        });
                    }
                    tokens.append_all(quote! {_ => Err(ParseError::InvalidValue) });
                    tokens
                };
                tokens.append_all(quote! {
                    match value {
                        #inner
                    }
                });
                tokens
            };
            tokens.append_all(quote! {
                impl std::str::FromStr for #name {
                    type Err = ParseError;

                    fn from_str(value: &str) -> Result<Self, Self::Err> {
                        #inner
                    }
                }
            });
        }
    }

    struct AnyRefFrom<'a> {
        name: &'a Ident,
        variants: &'a HashSet<String>,
    }
    impl ToTokens for AnyRefFrom<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let name = self.name;
            let inner = {
                let mut tokens = quote!();
                if self.variants.contains("Realm") {
                    tokens.append_all(quote! {
                        #name::Realm => Self::Realm,
                    });
                }
                if self.variants.contains("Framework") {
                    tokens.append_all(quote! {
                        #name::Framework => Self::Framework,
                    });
                }
                if self.variants.contains("Self_") {
                    tokens.append_all(quote! {
                        #name::Self_ => Self::Self_,
                    });
                }
                if self.variants.contains("Named") {
                    tokens.append_all(quote! {
                        #name::Named(ref n) => Self::Named(n),
                    });
                }
                tokens
            };
            tokens.append_all(quote! {
                impl<'a> From<&'a #name> for AnyRef<'a> {
                    fn from(r: &'a #name) -> Self {
                        match r {
                            #inner
                        }
                    }
                }
            });
        }
    }

    struct Deserialize<'a> {
        name: &'a Ident,
        expected: &'a syn::LitStr,
    }
    impl ToTokens for Deserialize<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let name = self.name;
            let expected = self.expected;
            let visit_str = gen_visit_str(None, self.expected);
            tokens.append_all(quote! {
                impl<'de> serde::de::Deserialize<'de> for #name {
                    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
                    where
                        D: serde::de::Deserializer<'de>,
                    {
                        struct RefVisitor;
                        impl<'de> serde::de::Visitor<'de> for RefVisitor {
                            type Value = #name;

                            fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
                                formatter.write_str(#expected)
                            }

                            #visit_str
                        }
                        deserializer.deserialize_str(RefVisitor)
                    }
                }
            });
        }
    }

    let display = Display { name: &attrs.name, variants: &attrs.variants };
    let from_str = FromStr { name: &attrs.name, variants: &attrs.variants };
    let anyref_from = AnyRefFrom { name: &attrs.name, variants: &attrs.variants };
    let deserialize = Deserialize { name: &attrs.name, expected: &attrs.expected };
    let tokens = quote! {
        #display

        #from_str

        #anyref_from

        #deserialize
    };
    Ok(tokens)
}

/// Attributes extracted from the `derive(Ref)` macro.
struct RefAttributes {
    /// Name of the reference enum.
    name: Ident,
    /// `expecting` string to return from the deserializer.
    expected: syn::LitStr,
    /// The variants that the reference can be. Valid choices: "named", "self", "realm",
    /// "framework".
    variants: HashSet<String>,
}

fn parse_ref_attributes(ast: &syn::DeriveInput) -> Result<RefAttributes, syn::Error> {
    let variants: HashSet<_> = if let syn::Data::Enum(enum_) = &ast.data {
        enum_.variants.iter().map(|v| v.ident.to_string()).collect()
    } else {
        return Err(syn::Error::new_spanned(ast, "Ref must be derived on an enum."));
    };
    let allowed_variants = hashset! {
        "Named",
        "Realm",
        "Framework",
        "Self_",
    };
    for ty in variants.iter() {
        if !allowed_variants.contains(ty as &str) {
            return Err(syn::Error::new_spanned(
                ast,
                &format!("enum variant not supported: {}", ty),
            ));
        }
    }
    let mut expected = None;
    for attr in &ast.attrs {
        if attr.path.is_ident("expected") {
            extract_expected(ast, attr, &mut expected)?;
        }
    }

    let expected =
        expected.ok_or_else(|| syn::Error::new_spanned(ast, "`expected` attribute is missing"))?;

    Ok(RefAttributes { name: ast.ident.clone(), expected, variants })
}

fn extract_expected(
    ast: &syn::DeriveInput,
    attr: &syn::Attribute,
    expected: &mut Option<syn::LitStr>,
) -> Result<(), syn::Error> {
    match attr
        .parse_meta()
        .map_err(|_| syn::Error::new_spanned(ast, "`expected` attribute must be name-value"))?
    {
        syn::Meta::NameValue(m) => match m.lit {
            syn::Lit::Str(l) => {
                if expected.is_some() {
                    return Err(syn::Error::new_spanned(ast, "duplicate `expected` attribute"));
                }
                *expected = Some(l);
            }
            _ => {
                return Err(syn::Error::new_spanned(
                    ast,
                    "`expected` attribute value must be string",
                ));
            }
        },
        _ => {
            return Err(syn::Error::new_spanned(ast, "`expected` attribute must be name-value"));
        }
    }
    Ok(())
}

fn extract_inner_type(
    ast: &syn::DeriveInput,
    attr: &syn::Attribute,
    inner_type: &mut Option<syn::Path>,
) -> Result<(), syn::Error> {
    match attr
        .parse_meta()
        .map_err(|_| syn::Error::new_spanned(ast, "`inner_type` attribute must be a path"))?
    {
        syn::Meta::List(l) => {
            if l.nested.len() != 1 {
                return Err(syn::Error::new_spanned(
                    ast,
                    "`inner_type` must contain exactly one path",
                ));
            }
            let nested_meta = l.nested.first().unwrap();
            match nested_meta {
                syn::NestedMeta::Meta(syn::Meta::Path(p)) => {
                    if inner_type.is_some() {
                        return Err(syn::Error::new_spanned(
                            ast,
                            "duplicate `inner_type` attribute",
                        ));
                    }
                    *inner_type = Some(p.clone());
                }
                _ => {
                    return Err(syn::Error::new_spanned(ast, "`inner_type` must contain a path"));
                }
            }
        }
        _ => {
            return Err(syn::Error::new_spanned(
                ast,
                "`inner_type` attribute value must be a path",
            ));
        }
    }
    Ok(())
}

fn extract_min_length(
    ast: &syn::DeriveInput,
    attr: &syn::Attribute,
    min_length: &mut Option<usize>,
) -> Result<(), syn::Error> {
    match attr
        .parse_meta()
        .map_err(|_| syn::Error::new_spanned(ast, "`min_length` attribute must be name-value"))?
    {
        syn::Meta::NameValue(m) => match m.lit {
            syn::Lit::Int(l) => {
                if min_length.is_some() {
                    return Err(syn::Error::new_spanned(ast, "duplicate `min_length` attribute"));
                }
                let l: usize = l.base10_parse().map_err(|_| {
                    syn::Error::new_spanned(ast, "`min_length` attribute is not base 10")
                })?;
                *min_length = Some(l);
            }
            _ => {
                return Err(syn::Error::new_spanned(
                    ast,
                    "`min_length` attribute value must be int",
                ));
            }
        },
        _ => {
            return Err(syn::Error::new_spanned(ast, "`min_length` attribute must be name-value"));
        }
    }
    Ok(())
}

fn extract_unique_items(
    ast: &syn::DeriveInput,
    attr: &syn::Attribute,
    unique_items: &mut Option<bool>,
) -> Result<(), syn::Error> {
    match attr
        .parse_meta()
        .map_err(|_| syn::Error::new_spanned(ast, "`unique_items` attribute must be name-value"))?
    {
        syn::Meta::NameValue(m) => match m.lit {
            syn::Lit::Bool(b) => {
                if unique_items.is_some() {
                    return Err(syn::Error::new_spanned(ast, "duplicate `unique_items` attribute"));
                }
                *unique_items = Some(b.value);
            }
            _ => {
                return Err(syn::Error::new_spanned(
                    ast,
                    "`unique_items` attribute value must be bool",
                ));
            }
        },
        _ => {
            return Err(syn::Error::new_spanned(
                ast,
                "`unique_items` attribute must be name-value",
            ));
        }
    }
    Ok(())
}
