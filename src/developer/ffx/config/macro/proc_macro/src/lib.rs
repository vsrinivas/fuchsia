// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    anyhow::{anyhow, bail, Context, Error, Result},
    proc_macro::TokenStream,
    proc_macro_hack::proc_macro_hack,
    quote::quote,
    serde_json::Value,
    std::convert::{TryFrom, TryInto},
    std::env,
};

const FFX_CONFIG_DEFAULT: &'static str = "ffx_config_default";

#[proc_macro_hack]
pub fn include_default(_input: TokenStream) -> TokenStream {
    // Test deserializing the default configuration file at compile time.
    let default = include_str!(env!("FFX_DEFAULT_CONFIG_JSON"));
    // This is being used as a way of validating the default json.
    // This should not happen as the JSON file is built using json_merge which does validation, but
    // leaving this here as the last line of defense.
    let _res: Option<Value> =
        serde_json::from_str(default).expect("default configuration is malformed");

    std::format!("Some(serde_json::json!({}))", default).parse().unwrap()
}

fn type_ident<'a>(ty: &'a syn::Type) -> Option<&'a syn::Ident> {
    if let syn::Type::Path(path) = ty {
        if path.qself.is_some() {
            return None;
        }
        // Checks last segment as the path might not necessarily be the type on
        // its own. This doesn't necessarily work with type aliases.
        let last_segment = path.path.segments.last()?;
        return Some(&last_segment.ident);
    }
    None
}

fn option_wrapped_type<'a>(ty: &'a syn::Type) -> Option<&'a syn::Type> {
    if let syn::Type::Path(path) = ty {
        if path.qself.is_some() {
            return None;
        }
        // Check last in the event that someone is using std::option::Option for some
        // reason.
        let last_segment = path.path.segments.last()?;
        if last_segment.ident == "Option" {
            if let syn::PathArguments::AngleBracketed(args) = &last_segment.arguments {
                let generic_args = args.args.first()?;
                if let syn::GenericArgument::Type(ty) = &generic_args {
                    return Some(ty);
                }
            }
        }
    }
    None
}

struct FfxConfigField<'a> {
    value_type: ConfigValueType,
    key: syn::LitStr,
    default: Option<syn::LitStr>,
    func_name: &'a syn::Ident,
}

impl<'a> FfxConfigField<'a> {
    fn parse(field: &'a syn::Field, attr: &syn::Attribute) -> Result<Self> {
        let wrapped_type =
            option_wrapped_type(&field.ty).ok_or(anyhow!("type must be wrapped in Option<_>"))?;
        let value_type: ConfigValueType =
            type_ident(wrapped_type).ok_or(anyhow!("couldn't get wrapped type"))?.try_into()?;
        let (key, default) = if let syn::Meta::List(meta_list) = attr.parse_meta()? {
            let mut key = None;
            let mut default = None;
            for kv in meta_list.nested.iter() {
                if let syn::NestedMeta::Meta(syn::Meta::NameValue(name)) = kv {
                    let value_to_update = match &name
                        .path
                        .segments
                        .last()
                        .expect("must have at least one segment")
                        .ident
                    {
                        n if n == "key" => &mut key,
                        n if n == "default" => &mut default,
                        n => panic!("unsupported ident: `{}`", n),
                    };
                    if let syn::Lit::Str(lit) = &name.lit {
                        *value_to_update = Some(lit.clone())
                    } else {
                        panic!(
                            "value for \"{}\" must be set to a string",
                            value_to_update.as_ref().expect("value set to empty").value()
                        )
                    }
                } else {
                    key = Some(
                        attr.parse_args::<syn::LitStr>()
                            .context("expecting config string with no default")?,
                    );
                    default = Option::<syn::LitStr>::None;
                    break;
                }
            }
            (key.ok_or(anyhow!("key expected"))?, default)
        } else {
            panic!("error parsing meta list. Is the attribute formtted correctly?");
        };
        let func_name =
            field.ident.as_ref().ok_or(anyhow!("ffx_config_default fields must have names"))?;
        Ok(Self { value_type, key, default, func_name })
    }
}

impl<'a> quote::ToTokens for FfxConfigField<'a> {
    fn to_tokens(&self, tokens: &mut proc_macro2::TokenStream) {
        let general_err = quote! { anyhow::anyhow!("invalid JSON value from config") };
        let conversion = match self.value_type {
            ConfigValueType::StringType => quote! {
                v.as_str().ok_or(#general_err)?.to_owned()
            },
            ConfigValueType::FloatType => quote! {
                v.as_f64().ok_or(#general_err)?
            },
        };
        let return_type = &self.value_type;
        let func_name = &self.func_name;
        let config_key = &self.key;
        let (return_value, top_level_return, conversion_res, backup_res) = match &self.default {
            Some(default) => (
                quote! { #return_type },
                quote! { t },
                quote! { #conversion },
                quote! { <#return_type as std::str::FromStr>::from_str(#default)? },
            ),
            None => (
                quote! { Option<#return_type> },
                quote! { Some(t) },
                quote! { Some(#conversion) },
                quote! { None },
            ),
        };
        tokens.extend(quote! {
            pub async fn #func_name(&self) -> anyhow::Result<#return_value> {
                let field = self.#func_name.clone();
                Ok(if let Some(t) = field {
                    #top_level_return
                } else {
                    let cfg_value: Option<ffx_config::Value> = ffx_config::get(#config_key).await.ok();
                    if let Some(v) = cfg_value {
                        #conversion_res
                    } else {
                        #backup_res
                    }
                })
            }
        });
    }
}

enum ConfigValueType {
    StringType,
    FloatType,
}

impl quote::ToTokens for ConfigValueType {
    fn to_tokens(&self, tokens: &mut proc_macro2::TokenStream) {
        match self {
            ConfigValueType::StringType => tokens.extend(quote! { String }),
            ConfigValueType::FloatType => tokens.extend(quote! { f64 }),
        }
    }
}

impl TryFrom<&syn::Ident> for ConfigValueType {
    type Error = Error;

    fn try_from(value: &syn::Ident) -> Result<Self> {
        Ok(match value {
            n if n == "String" => ConfigValueType::StringType,
            n if n == "f64" => ConfigValueType::FloatType,
            _ => bail!("unsupported type: {}", value),
        })
    }
}

fn generate_impls(item: &syn::ItemStruct) -> TokenStream {
    let mut configs = Vec::new();
    let mut runtime_config_builder = Vec::new();
    runtime_config_builder.push(quote! {
        let mut config_builder = Option::<String>::None;
    });
    for field in item.fields.iter() {
        if let Some(attr) = field.attrs.iter().find(|a| a.path.is_ident(FFX_CONFIG_DEFAULT)) {
            let config_field = FfxConfigField::parse(field, attr).unwrap();
            let field = &config_field.func_name;
            let key = &config_field.key;
            runtime_config_builder.push(quote! {
                // TODO(awdavies): Perhaps this should do something with the
                // default value (since this isn't a necessary if/else)?
                let append_string = if let Some(t) = &self.#field {
                    Some(format!("{}={}", #key, t))
                } else {
                    None
                };
                if let Some(s) = append_string {
                    if let Some(c) = &mut config_builder {
                        c.push_str(format!(",{}", s).as_str());
                    } else {
                        config_builder = Some(s);
                    }
                }
            });
            configs.push(config_field);
        }
    }
    runtime_config_builder.push(quote! {
        config_builder
    });
    let struct_name = &item.ident;
    TokenStream::from(quote! {
        impl #struct_name {
            #(#configs)*

            pub fn runtime_config_overrides(&self) -> Option<String>  {
                #(#runtime_config_builder)*
            }
        }
    })
}

#[proc_macro_derive(FfxConfigBacked, attributes(ffx_config_default))]
pub fn derive_ffx_config_backed(input: TokenStream) -> TokenStream {
    let item: syn::ItemStruct = syn::parse(input.into()).expect("expected struct");
    let impls = generate_impls(&item);
    impls
}
