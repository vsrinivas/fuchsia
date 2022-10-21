// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::ParseError;
use crate::types::{FfxAttrTy, NamedField, NamedFieldTy};
use proc_macro2::{Span, TokenStream};
use quote::{format_ident, quote, quote_spanned, ToTokens};
use syn;
use syn::spanned::Spanned;

/// Creates an assert to ensure that a type implements TryFromEnv.
struct TryFromEnvTypeAssertion<'a> {
    /// Used to create a unique generic assert struct name. It's the caller's
    /// responsibility to ensure this is unique.
    id: usize,
    field: NamedField<'a>,
}

impl ToTokens for TryFromEnvTypeAssertion<'_> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let name = self.field.field_name;
        let assert_name = format_ident!("{}{}", "_AssertTryFrom", self.id, span = name.span());
        let ty = self.field.field_ty;
        let ty_span = ty.span();
        tokens.extend(quote_spanned! {ty_span=>
            struct #assert_name where #ty: fho::TryFromEnv;
        })
    }
}

/// Creates the top-level struct declaration before any brackets are used.
///
/// This would be used like so in a quote:
/// ```rust
/// let struct_decl = StructDecl(&ast);
/// quote! {
///     #struct_decl {
///         /* ... */
///     }
/// }
/// ```
struct StructDecl<'a>(&'a syn::DeriveInput);

impl ToTokens for StructDecl<'_> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let ast = self.0;
        let struct_name = &ast.ident;
        let (impl_generics, ty_generics, where_clause) = ast.generics.split_for_impl();
        tokens.extend(quote! {
            #[async_trait::async_trait(?Send)]
            impl #impl_generics FfxTool for #struct_name #ty_generics #where_clause
        })
    }
}

/// Creates a TryFromEnv invocation for the given type.
struct TryFromEnvInvocation<'a>(&'a syn::Type);

impl ToTokens for TryFromEnvInvocation<'_> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let ty = self.0;
        let ty_span = ty.span();
        tokens.extend(quote_spanned! {ty_span=>
            // _env comes from the from_env function as part of the TryFromEnv
            // trait.
            <#ty as fho::TryFromEnv>::try_from_env(&_env)
        });
    }
}

/// Declares a command type which is used for the TryFromEnv trait.
///
/// Creates `type Command = FooType;`
struct CommandFieldTypeDecl<'a>(NamedField<'a>);

impl ToTokens for CommandFieldTypeDecl<'_> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let command_field = &self.0;
        let ty = command_field.field_ty;
        let ty_span = ty.span();
        tokens.extend(quote_spanned! {ty_span=>
            type Command = #ty;
        })
    }
}

/// Contains a collection of variables that will be created using TryFromEnv.
///
/// Each variable will have an assertion for the type (used to surface errors to
/// the appropriate span), the names of the values that are going to be joined,
/// and the try_from_env invocations of each type.
///
/// The `ToTokens` implementation of this struct only creates a `let (foo, bar) =
/// try_join!(Foo::try_from_env(_env), Bar::try_from_env(_env)` invocation.
///
/// This struct is used in a sort of sandwich fashion. Here's an example:
///
/// ```rust
/// let vcc = VariableCollection::new();
/// /* fill it with values using the impl API */
/// let asserts = vcc.try_from_env_type_assertions;
/// let results_names = join_results_names;
/// quote! {
/// #(#asserts)*
/// fn try_from_env(_env: FhoEnvironment<'_>) -> Result<Self> {
///
///     #vcc
///
///     Self {
///         #(#results_names,)*
///     }
/// }
/// }
/// ```
///
/// This will create the variables using an allocation statement, then allow for their
/// names to be put into the struct (as these are intended to be derived from the struct
/// field names).
#[derive(Default)]
struct VariableCreationCollection<'a> {
    try_from_env_type_assertions: Vec<TryFromEnvTypeAssertion<'a>>,
    join_results_names: Vec<&'a syn::Ident>,
    try_from_env_invocations: Vec<TryFromEnvInvocation<'a>>,
}

impl<'a> VariableCreationCollection<'a> {
    fn new() -> Self {
        Self::default()
    }

    /// Will panic if being given a `#[command]` field.
    fn add_field(&mut self, field: NamedFieldTy<'a>) {
        match field {
            NamedFieldTy::Blank(field) => {
                self.try_from_env_invocations.push(TryFromEnvInvocation(field.field_ty));
                self.join_results_names.push(field.field_name);
                let id = self.try_from_env_type_assertions.len() + 1;
                self.try_from_env_type_assertions.push(TryFromEnvTypeAssertion { id, field });
            }
            _ => panic!("unexpected non-blank field type"),
        }
    }
}

impl ToTokens for VariableCreationCollection<'_> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let Self { join_results_names, try_from_env_invocations, .. } = self;
        let join_results_statement = if join_results_names.is_empty() {
            quote!()
        } else if join_results_names.len() == 1 {
            quote! {
                let #(#join_results_names),* = #(#try_from_env_invocations),*.await?;
            }
        } else {
            quote! {
                let (#(#join_results_names),*) = fho::macro_deps::futures::try_join!(#(#try_from_env_invocations),*)?;
            }
        };
        tokens.extend(join_results_statement);
    }
}

pub struct NamedFieldStruct<'a> {
    forces_stdout_logs: bool,
    command_field_decl: CommandFieldTypeDecl<'a>,
    struct_decl: StructDecl<'a>,
    vcc: VariableCreationCollection<'a>,
}

fn extract_command_field<'a>(
    fields: &mut Vec<NamedFieldTy<'a>>,
) -> Result<NamedField<'a>, ParseError> {
    let command_field_idx = fields
        .iter()
        .position(|f| matches!(f, NamedFieldTy::Command(_)))
        .ok_or(ParseError::CommandRequired(Span::call_site()))?;
    match fields.remove(command_field_idx) {
        NamedFieldTy::Command(f) => Ok(f),
        _ => unreachable!(),
    }
}

impl<'a> NamedFieldStruct<'a> {
    pub fn new(
        parent_ast: &'a syn::DeriveInput,
        fields: &'a syn::FieldsNamed,
    ) -> Result<Self, ParseError> {
        let mut fields =
            fields.named.iter().map(NamedFieldTy::parse).collect::<Result<Vec<_>, ParseError>>()?;
        let command_field_decl = CommandFieldTypeDecl(extract_command_field(&mut fields)?);
        let struct_decl = StructDecl(&parent_ast);
        let forces_stdout_logs = match FfxAttrTy::parse(&parent_ast.attrs)? {
            Some(FfxAttrTy::ForcesStdoutLogs) => true,
            _ => false,
        };
        let mut vcc = VariableCreationCollection::new();
        for field in fields.into_iter() {
            vcc.add_field(field);
        }
        Ok(Self { forces_stdout_logs, command_field_decl, struct_decl, vcc })
    }
}

impl ToTokens for NamedFieldStruct<'_> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let Self { forces_stdout_logs, command_field_decl, struct_decl, vcc } = self;
        let command_field_name = command_field_decl.0.field_name;
        let try_from_env_type_assertions = &vcc.try_from_env_type_assertions;
        let join_results_names = &vcc.join_results_names;
        let span = Span::call_site();
        let res = quote_spanned! {span=>
            #(#try_from_env_type_assertions)*
            #struct_decl {
                #command_field_decl
                async fn from_env(
                    _env: fho::FhoEnvironment<'_>,
                    cmd: Self::Command,
                ) -> Result<Self> {
                    // Allow unused in the event that things don't compile (then
                    // this will mark the error as coming from the span for the name of the
                    // command).
                    #[allow(unused)]
                    use fho::TryFromEnv;

                    #vcc

                    Ok(Self {
                        #(#join_results_names,)*
                        #command_field_name: cmd
                    })
                }

                fn forces_stdout_log() -> bool {
                    #forces_stdout_logs
                }
            }
        };
        tokens.extend(res);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::parse_macro_derive;

    #[test]
    fn test_vcc_empty() {
        let vcc = VariableCreationCollection::new();
        assert_eq!("", vcc.into_token_stream().to_string());
    }

    #[test]
    fn test_vcc_single_element() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            struct Foo {
                bar: u32,
            }
            "#,
        );
        let ds = crate::extract_struct_info(&ast).unwrap();
        let mut fields = match &ds.fields {
            syn::Fields::Named(fields) => fields
                .named
                .iter()
                .map(NamedFieldTy::parse)
                .collect::<Result<Vec<_>, ParseError>>()
                .unwrap(),
            _ => unreachable!(),
        };
        let mut vcc = VariableCreationCollection::new();
        vcc.add_field(fields.remove(0));
        // An unfortunate side effect of translating to tokens is that every individual token is
        // separated by a space.
        assert_eq!(
            "let bar = < u32 as fho :: TryFromEnv > :: try_from_env (& _env) . await ? ;",
            vcc.into_token_stream().to_string(),
        );
    }

    #[test]
    fn test_vcc_multiple_elements() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            struct Foo {
                bar: u32,
                baz: u8,
            }
            "#,
        );
        let ds = crate::extract_struct_info(&ast).unwrap();
        let mut fields = match &ds.fields {
            syn::Fields::Named(fields) => fields
                .named
                .iter()
                .map(NamedFieldTy::parse)
                .collect::<Result<Vec<_>, ParseError>>()
                .unwrap(),
            _ => unreachable!(),
        };
        let mut vcc = VariableCreationCollection::new();
        vcc.add_field(fields.remove(0));
        vcc.add_field(fields.remove(0));
        assert_eq!(
            "let (bar , baz) = fho :: macro_deps :: futures :: try_join ! (< u32 as fho :: TryFromEnv > :: try_from_env (& _env) , < u8 as fho :: TryFromEnv > :: try_from_env (& _env)) ? ;",
            vcc.into_token_stream().to_string(),
        );
    }
}
