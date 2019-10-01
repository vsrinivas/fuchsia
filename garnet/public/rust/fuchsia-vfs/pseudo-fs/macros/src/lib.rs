// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

//! Macros that help with pseudo directory tries generation.

extern crate proc_macro;

use {
    proc_macro::TokenStream,
    proc_macro2::Span,
    proc_macro_hack::proc_macro_hack,
    quote::{quote, quote_spanned, ToTokens},
    std::collections::HashSet,
    syn::{
        parse::{Parse, ParseStream},
        parse_macro_input, parse_quote,
        spanned::Spanned,
        Expr, Ident, LitByteStr, LitStr, Path, Token,
    },
};

mod kw {
    syn::custom_keyword!(protection_attributes);
}

/// See [fuchsia-vfs/pseudo-fs/src/lib.rs] for the documentation for this macro usage.
//
// TODO It would be nice to provide support for "shorthand" syntax for the on_read expression.
// When the value of the expression is a String, &str, &[u8], or &[u8; N] it can be trivially
// wrapped in a closure that returns the expression.  I think a way to do it is to change
// read_only, write_only and read_write to accept a type that accepts conversion from all of these
// types, wrapping them into closures when necessary.
//
// It is probably mostly useful for tests, as actual users will most likely use non-trivial files,
// thus requiring a closure.
//
// This macro is tested by tests in the fuchsia_vfs_pseudo_fs::directory module.
//
// TODO Unfortunately there are no tests for error cases in there.  We should add several test
// cases into this crate directly, to make sure we report errors and report them in the right
// locations, with proper error messages, etc.
#[proc_macro_hack]
pub fn pseudo_directory(input: TokenStream) -> TokenStream {
    let parsed = parse_macro_input!(input as PseudoDirectory);

    // Should be def_site, but that's behind cfg(procmacro2_semver_exempt).
    let span = Span::call_site();

    let directory_mod: Path = parse_quote!(::fuchsia_vfs_pseudo_fs::directory);
    let macro_mod: Path = parse_quote!(::fuchsia_vfs_pseudo_fs::pseudo_directory);
    // Remove the underscores when span becomes Span::def_site().
    let dir_var = Ident::new("__dir", span);

    let constructor = match parsed.protection_attributes {
        None => quote! {
            let mut #dir_var = #directory_mod::simple::empty();
        },
        Some(attrs) => quote! {
            let mut #dir_var = #directory_mod::simple::empty_attr(#attrs);
        },
    };

    let entries = parsed.entries.into_iter().map(|DirectoryEntry { name, entry }| {
        let location = format!("{:?}", Spanned::span(&name));
        quote! {
            #macro_mod::unwrap_add_entry_span(#name, #location,
                                              #dir_var.add_entry(#name, #entry));
        }
    });

    TokenStream::from(quote! {
        {
            #constructor

            #( #entries )*

            #dir_var
        }
    })
}

#[derive(Debug)]
struct ProtectionAttributes {
    attrs: Expr,
}

impl Parse for ProtectionAttributes {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        Ok(ProtectionAttributes { attrs: input.parse::<Expr>()? })
    }
}

impl ToTokens for ProtectionAttributes {
    fn to_tokens(&self, tokens: &mut proc_macro2::TokenStream) {
        self.attrs.to_tokens(tokens);
    }
}

struct DirectoryEntry {
    name: proc_macro2::TokenStream,
    entry: Expr,
}

impl Parse for DirectoryEntry {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        let mut seen = HashSet::new();
        let mut check_literal = |name: String, span| {
            // Can not statically check the name length.  I need fidl_fuchsia_io::MAX_FILENAME
            // which is generated from FIDL, and that generation is not implemented for the host
            // build. And this create is compiled for the host.

            if seen.insert(name.clone()) {
                return Ok(());
            }

            let message = format!(
                "Duplicate literal entry name '{}'. There is another literal \
                 before this one with the same value.",
                name
            );
            Err(syn::Error::new(span, message))
        };

        let name = {
            let lookahead = input.lookahead1();
            if lookahead.peek(LitStr) {
                let str_lit = input.parse::<LitStr>()?;
                check_literal(str_lit.value(), str_lit.span())?;
                str_lit.into_token_stream()
            } else if lookahead.peek(LitByteStr) {
                let byte_str_lit = input.parse::<LitByteStr>()?;
                match String::from_utf8(byte_str_lit.value()) {
                    Ok(value) => check_literal(value, byte_str_lit.span())?,
                    Err(err) => {
                        let text =
                            format!("Entry names should be valid UTF-8: {}", err.utf8_error());
                        return Err(input.error(text));
                    }
                }
                quote_spanned! {byte_str_lit.span()=>
                    unsafe { String::from_utf8_unchecked(#byte_str_lit.to_vec()).as_str() }
                }
            } else {
                match input.parse::<Expr>() {
                    Ok(name) => name.into_token_stream(),
                    Err(err) => {
                        return Err(input.error(format!(
                            "Failed to parse as a string, a byte string or an expression. \
                             Expression parse error: {}",
                            err
                        )));
                    }
                }
            }
        };
        input.parse::<Token![=>]>()?;
        let entry = input.parse::<Expr>()?;
        Ok(DirectoryEntry { name, entry })
    }
}

struct PseudoDirectory {
    protection_attributes: Option<ProtectionAttributes>,
    entries: Vec<DirectoryEntry>,
}

/// A helper that tries to parse the "protection_attributes" keyword, followed by :", followed by
/// an expression, followed by ';' as a ProtectionAttributes values.  Returns None, when the
/// keyword is not found.
fn parse_protection_attributes(
    input: ParseStream<'_>,
) -> syn::Result<Option<ProtectionAttributes>> {
    if !input.peek(kw::protection_attributes) {
        return Ok(None);
    }

    input.parse::<kw::protection_attributes>()?;
    input.parse::<Token![:]>()?;

    let res = input.parse::<ProtectionAttributes>()?;
    input.parse::<Token![;]>()?;

    Ok(Some(res))
}

impl Parse for PseudoDirectory {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        let protection_attributes = parse_protection_attributes(input)?;

        let entries: Vec<DirectoryEntry> =
            input.parse_terminated::<_, Token![,]>(DirectoryEntry::parse)?.into_iter().collect();

        Ok(PseudoDirectory { protection_attributes, entries })
    }
}
