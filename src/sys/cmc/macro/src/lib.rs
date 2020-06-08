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
/// - `parse_error`: The `RefValidationError` variant to return when `FromStr` fails.
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
/// #[parse_error(ValidationError::RegistrationRefInvalid)]
/// pub enum RegistrationRef {
///     /// A reference to a child.
///     Named(Name),
///     /// A reference to the containing realm.
///     Realm,
///     /// A reference to this component.
///     Self_,
/// }
/// ```
#[proc_macro_derive(Ref, attributes(expected, parse_error))]
pub fn derive_ref(input: TokenStream) -> TokenStream {
    let ast = syn::parse(input).expect("could not parse input");
    impl_derive_ref(ast)
}

fn impl_derive_ref(ast: syn::DeriveInput) -> TokenStream {
    let attrs = parse_attributes(ast);

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
        parse_error: &'a syn::Path,
    }
    impl ToTokens for FromStr<'_> {
        fn to_tokens(&self, tokens: &mut TokenStream2) {
            let name = self.name;
            let parse_error = self.parse_error;
            let inner = {
                let mut tokens = quote!();
                tokens.append_all(quote! {
                    if value.len() > 101 {
                        return Err(ValidationError::RefInvalid(RefValidationError::RefTooLong));
                    }
                });
                if self.variants.contains("Named") {
                    tokens.append_all(quote! {
                        if value.starts_with("#") {
                            return value[1..]
                                .parse::<Name>()
                                .map(Self::Named)
                                .map_err(|_| ValidationError::NameInvalid(
                                    NameValidationError::MalformedName));
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
                    tokens
                        .append_all(quote! {_ => Err(ValidationError::RefInvalid(#parse_error)),});
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
                    type Err = ValidationError;

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

                            fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
                            where
                                E: serde::de::Error,
                            {
                                value
                                    .parse()
                                    .map_err(|e| E::custom(format!("{}", e)))
                            }
                        }
                        deserializer.deserialize_str(RefVisitor)
                    }
                }
            });
        }
    }

    let display = Display { name: &attrs.name, variants: &attrs.variants };
    let from_str =
        FromStr { name: &attrs.name, variants: &attrs.variants, parse_error: &attrs.parse_error };
    let anyref_from = AnyRefFrom { name: &attrs.name, variants: &attrs.variants };
    let deserialize = Deserialize { name: &attrs.name, expected: &attrs.expected };
    let tokens = quote! {
        #display

        #from_str

        #anyref_from

        #deserialize
    };
    tokens.into()
}

/// Attributes extracted from the `derive` macro.
struct Attributes {
    /// Name of the reference enum.
    name: Ident,
    /// `expecting` string to return from the deserializer.
    expected: syn::LitStr,
    /// The variant of `ValidationError` to return when parsing fails.
    parse_error: syn::Path,
    /// The variants that the reference can be. Valid choices: "named", "self", "realm",
    /// "framework".
    variants: HashSet<String>,
}

fn parse_attributes(ast: syn::DeriveInput) -> Attributes {
    let variants: HashSet<_> = if let syn::Data::Enum(enum_) = ast.data {
        enum_.variants.iter().map(|v| v.ident.to_string()).collect()
    } else {
        panic!("Ref must be derived on an enum.");
    };
    let allowed_variants = hashset! {
        "Named",
        "Realm",
        "Framework",
        "Self_",
    };
    for ty in variants.iter() {
        if !allowed_variants.contains(ty as &str) {
            panic!("enum variant not supported: {}", ty);
        }
    }
    let mut expected = None;
    let mut parse_error = None;
    for attr in ast.attrs {
        if attr.path.is_ident("expected") {
            match attr.parse_meta().expect("`expected` attribute must be name-value") {
                syn::Meta::NameValue(m) => match m.lit {
                    syn::Lit::Str(l) => {
                        if expected.is_some() {
                            panic!("duplicate `expected` attribute");
                        }
                        expected = Some(l);
                    }
                    _ => {
                        panic!("`expected` attribute value must be string");
                    }
                },
                _ => {
                    panic!("`expected` attribute must be name-value");
                }
            }
        } else if attr.path.is_ident("parse_error") {
            match attr.parse_meta().expect("`parse_error` attribute must be a path") {
                syn::Meta::List(l) => {
                    if l.nested.len() != 1 {
                        panic!("`parse_error` must contain exactly one path");
                    }
                    let nested_meta = l.nested.first().unwrap();
                    match nested_meta {
                        syn::NestedMeta::Meta(syn::Meta::Path(p)) => {
                            if parse_error.is_some() {
                                panic!("duplicate `parse_error` attribute");
                            }
                            parse_error = Some(p.clone());
                        }
                        _ => {
                            panic!("`parse_error` must contain a path");
                        }
                    }
                }
                _ => {
                    panic!("`parse_error` attribute value must be a path");
                }
            }
        }
    }

    let expected = expected.expect("`expected` attribute is missing");
    let parse_error = parse_error.expect("`parse_error` attribute is missing");

    Attributes { name: ast.ident, expected, parse_error, variants }
}
