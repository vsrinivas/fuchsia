// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::common::{extract_expected, gen_visit_str, ident_from_path},
    maplit::hashset,
    proc_macro2::{Ident, TokenStream as TokenStream2},
    quote::{quote, ToTokens, TokenStreamExt},
    std::collections::HashSet,
    syn,
};

pub fn impl_derive_ref(ast: syn::DeriveInput) -> Result<TokenStream2, syn::Error> {
    let attrs = parse_reference_attributes(&ast)?;

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
                if self.variants.contains("Parent") {
                    tokens.append_all(quote! {
                        Self::Parent => write!(f, "parent"),
                    });
                }
                if self.variants.contains("Framework") {
                    tokens.append_all(quote! {
                        Self::Framework => write!(f, "framework"),
                    });
                }
                if self.variants.contains("Debug") {
                    tokens.append_all(quote! {
                        Self::Debug => write!(f, "debug"),
                    });
                }
                if self.variants.contains("Self_") {
                    tokens.append_all(quote! {
                        Self::Self_ => write!(f, "self"),
                    });
                }
                if self.variants.contains("Void") {
                    tokens.append_all(quote! {
                        Self::Void => write!(f, "void"),
                    });
                }
                if self.variants.contains("All") {
                    tokens.append_all(quote! {
                        Self::All => write!(f, "all"),
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
                    if self.variants.contains("Parent") {
                        tokens.append_all(quote! {
                            "parent" => Ok(Self::Parent),
                        });
                    }
                    if self.variants.contains("Framework") {
                        tokens.append_all(quote! {
                            "framework" => Ok(Self::Framework),
                        });
                    }
                    if self.variants.contains("Debug") {
                        tokens.append_all(quote! {
                            "debug" => Ok(Self::Debug),
                        });
                    }
                    if self.variants.contains("Self_") {
                        tokens.append_all(quote! {
                            "self" => Ok(Self::Self_),
                        });
                    }
                    if self.variants.contains("Void") {
                        tokens.append_all(quote! {
                            "void" => Ok(Self::Void),
                        });
                    }
                    if self.variants.contains("All") {
                        tokens.append_all(quote! { "all" => Ok(Self::All), });
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
                if self.variants.contains("Parent") {
                    tokens.append_all(quote! {
                        #name::Parent => Self::Parent,
                    });
                }
                if self.variants.contains("Framework") {
                    tokens.append_all(quote! {
                        #name::Framework => Self::Framework,
                    });
                }
                if self.variants.contains("Debug") {
                    tokens.append_all(quote! {
                        #name::Debug => Self::Debug,
                    });
                }
                if self.variants.contains("Self_") {
                    tokens.append_all(quote! {
                        #name::Self_ => Self::Self_,
                    });
                }
                if self.variants.contains("Void") {
                    tokens.append_all(quote! {
                        #name::Void => Self::Void,
                    });
                }
                if self.variants.contains("Named") {
                    tokens.append_all(quote! {
                        #name::Named(ref n) => Self::Named(n),
                    });
                }
                if self.variants.contains("All") {
                    tokens.append_all(quote! {
                        #name::All => panic!("should not convert All to AnyRef"),
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

    struct DeserializeAndSerialize<'a> {
        name: &'a Ident,
        expected: &'a syn::LitStr,
    }
    impl ToTokens for DeserializeAndSerialize<'_> {
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

                impl serde::ser::Serialize for #name {
                    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
                    where
                        S: serde::ser::Serializer
                    {
                        self.to_string().serialize(serializer)
                    }
                }
            });
        }
    }

    let display = Display { name: &attrs.name, variants: &attrs.variants };
    let from_str = FromStr { name: &attrs.name, variants: &attrs.variants };
    let anyref_from = AnyRefFrom { name: &attrs.name, variants: &attrs.variants };
    let deserialize_and_serialize =
        DeserializeAndSerialize { name: &attrs.name, expected: &attrs.expected };
    let tokens = quote! {
        #display

        #from_str

        #anyref_from

        #deserialize_and_serialize
    };
    Ok(tokens)
}

/// Attributes extracted from the `derive(Reference)` macro.
struct ReferenceAttributes {
    /// Name of the reference enum.
    name: Ident,
    /// `expecting` string to return from the deserializer.
    expected: syn::LitStr,
    /// The variants that the reference can be. Valid choices: "named", "self", "parent",
    /// "framework".
    variants: HashSet<String>,
}

fn parse_reference_attributes(ast: &syn::DeriveInput) -> Result<ReferenceAttributes, syn::Error> {
    let variants: HashSet<_> = if let syn::Data::Enum(enum_) = &ast.data {
        enum_.variants.iter().map(|v| v.ident.to_string()).collect()
    } else {
        return Err(syn::Error::new_spanned(ast, "Ref must be derived on an enum."));
    };
    let allowed_variants = hashset! {
        "Named",
        "Parent",
        "Framework",
        "Debug",
        "Self_",
        "Void",
        "All",
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
        if !attr.path.is_ident("reference") {
            continue;
        }
        match attr
            .parse_meta()
            .map_err(|_| syn::Error::new_spanned(ast, "`reference` attribute is not valid"))?
        {
            syn::Meta::List(l) => {
                for attr in l.nested {
                    match attr {
                        syn::NestedMeta::Meta(syn::Meta::NameValue(attr)) => {
                            let ident = ident_from_path(&attr.path);
                            match &ident as &str {
                                "expected" => extract_expected(ast, attr, &mut expected)?,
                                _ => {
                                    return Err(syn::Error::new_spanned(
                                        ast,
                                        "`reference` attribute is not valid",
                                    ));
                                }
                            }
                        }
                        _ => {
                            return Err(syn::Error::new_spanned(
                                ast,
                                "`reference` attribute must contain name-value pairs",
                            ))?
                        }
                    }
                }
            }
            _ => {
                return Err(syn::Error::new_spanned(
                    ast,
                    "`reference` attribute value must be a list",
                ));
            }
        }
    }

    let expected =
        expected.ok_or_else(|| syn::Error::new_spanned(ast, "`expected` attribute is missing"))?;

    Ok(ReferenceAttributes { name: ast.ident.clone(), expected, variants })
}
