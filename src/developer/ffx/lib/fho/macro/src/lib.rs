// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use errors::ParseError;
use proc_macro::TokenStream;
use proc_macro2::Span;
use quote::ToTokens;
use structs::NamedFieldStruct;

mod errors;
mod structs;
mod testing;
mod types;

pub(crate) fn extract_struct_info<'a>(
    ast: &'a syn::DeriveInput,
) -> Result<&'a syn::DataStruct, ParseError> {
    match &ast.data {
        syn::Data::Struct(ds) => Ok(ds),
        _ => Err(ParseError::OnlyStructsSupported(Span::call_site())),
    }
}

fn generate_struct_impl(ast: &syn::DeriveInput) -> Result<proc_macro2::TokenStream, ParseError> {
    let ds = extract_struct_info(ast)?;
    match &ds.fields {
        syn::Fields::Named(fields) => {
            let tokens = NamedFieldStruct::new(ast, fields)?.into_token_stream();
            Ok(tokens)
        }
        _ => Err(ParseError::OnlyNamedFieldStructsSupported(Span::call_site())),
    }
}

#[proc_macro_derive(FfxTool, attributes(command, ffx))]
pub fn ffx_tool_derive(input: TokenStream) -> TokenStream {
    let ast = syn::parse_macro_input!(input as syn::DeriveInput);
    match generate_struct_impl(&ast) {
        Ok(r) => r,
        Err(e) => e.into_token_stream(),
    }
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use testing::parse_macro_derive;

    // Lots of the tests in here are more end-to-end style, because there aren't any methods to
    // parse things like a `Field` without using a token stream or parse string, and due to the
    // weirdness of proc_macro::TokenStream crossing library boundaries we don't appear to be able
    // to use it for parsing, so this is all done using strings instead.
    #[test]
    fn test_named_field_struct() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            struct FooStruct {
                #[command]
                foo_command: Command,
            }
            "#,
        );
        let _ = generate_struct_impl(&ast).unwrap();
    }

    #[test]
    fn test_named_field_struct_duplicate_field_attr() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            struct FooStruct {
                #[command]
                #[command]
                foo_command: Command,
            }
            "#,
        );
        let res = generate_struct_impl(&ast).unwrap_err();
        assert_matches!(res, ParseError::DuplicateAttr(_));
    }

    #[test]
    fn test_non_struct_failure() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            enum Foo {
                Bar,
                Baz,
            }
            "#,
        );
        let res = generate_struct_impl(&ast).unwrap_err();
        assert_matches!(res, ParseError::OnlyStructsSupported(_));
    }

    #[test]
    fn test_unit_struct_failure() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            struct Foo;
            "#,
        );
        let res = generate_struct_impl(&ast).unwrap_err();
        assert_matches!(res, ParseError::OnlyNamedFieldStructsSupported(_));
    }

    #[test]
    fn test_struct_no_command_field_failure() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            struct Foo {
                bar: u32,
            }
            "#,
        );
        let res = generate_struct_impl(&ast).unwrap_err();
        assert_matches!(res, ParseError::CommandRequired(_));
    }
}
