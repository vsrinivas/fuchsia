// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

//! Macros that help with pseudo directory tree generation.

#[cfg(test)]
mod tests;

extern crate proc_macro;

use {
    proc_macro2::{Span, TokenStream},
    quote::{quote, quote_spanned, ToTokens},
    std::collections::HashSet,
    syn::{
        buffer::TokenBuffer,
        parse::{Parse, ParseStream},
        parse_quote,
        spanned::Spanned,
        Expr, Ident, LitByteStr, LitStr, Path, Token,
    },
};

/// See [//src/lib/storage/vfs/rust:vfs/src/lib.rs] for the documentation for this macro usage.
//
// TODO(fxbug.dev/35904) It would be nice to provide support for "shorthand" syntax for the on_read
// expression.  When the value of the expression is a String, &str, &[u8], or &[u8; N] it can be
// trivially wrapped in a closure that returns the expression.  I think a way to do it is to change
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
#[proc_macro]
pub fn pseudo_directory(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    // In order to be able to unit tests the macro implementation we switch to
    // `proc_macro2::TokenStream`.  `proc_macro::TokenStream` can not be easily constructed from an
    // `str` when not connected to the compiler, while `proc_macro2::TokenStream` does provide for
    // this functionality.
    pseudo_directory_impl(false, input.into()).into()
}

#[proc_macro]
pub fn mut_pseudo_directory(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    // In order to be able to unit tests the macro implementation we switch to
    // `proc_macro2::TokenStream`.  `proc_macro::TokenStream` can not be easily constructed from an
    // `str` when not connected to the compiler, while `proc_macro2::TokenStream` does provide for
    // this functionality.
    pseudo_directory_impl(true, input.into()).into()
}

/// Our own version of [`fidl_fuchsia_io::MAX_FILENAME`].  We can not depend on the fidl_fuchsia_io as
/// there is not FIDL generation for the host.  So we keep our own copy and use
/// [`pseudo_directory::check_max_filename_constant`] to make sure these two constants are in sync.
const MAX_FILENAME: u64 = 255;

/// A helper macro to expose the [`MAX_FILENAME`] value to tests in the [`fuchsia_vfs_pseudo_fs`]
/// crate.
#[proc_macro]
pub fn pseudo_directory_max_filename(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let res = if !input.is_empty() {
        let span = TokenBuffer::new(input).begin().span();
        quote_spanned! {span=>
            compile_error!("`pseudo_directory_max_filename!` should be given no input")
        }
    } else {
        quote! { #MAX_FILENAME }
    };

    proc_macro::TokenStream::from(res)
}

fn pseudo_directory_impl(mutable: bool, input: TokenStream) -> TokenStream {
    let parsed = match syn::parse2::<PseudoDirectory>(input) {
        Ok(tree) => tree,
        Err(err) => {
            return TokenStream::from(err.to_compile_error());
        }
    };

    // Should be def_site, but it is behind cfg(procmacro2_semver_exempt).
    let span = Span::call_site();

    let directory_mod: Path = parse_quote!(::vfs::directory);
    let specific_directory_type: Path = if mutable {
        parse_quote!(#directory_mod::mutable::simple)
    } else {
        parse_quote!(#directory_mod::immutable::simple)
    };
    let macro_mod: Path = parse_quote!(::vfs::pseudo_directory);

    let (dir_var, constructor, result) = match parsed.assign_to {
        Some(ident) => (
            ident.clone(),
            quote! {
                #ident = #specific_directory_type();
            },
            quote! { #ident.clone() },
        ),
        None => {
            // TODO(fxbug.dev/35905) Remove the underscores when span becomes Span::def_site().
            let ident = Ident::new("__dir", span);
            (
                ident.clone(),
                quote! {
                    let #ident = #specific_directory_type();
                },
                quote! { #ident },
            )
        }
    };

    let entries = parsed.entries.into_iter().map(|DirectoryEntry { name, entry }| {
        let location = format!("{:?}", Spanned::span(&name));
        quote! {
            #macro_mod::unwrap_add_entry_span(#name, #location,
                                              #dir_var.clone().add_entry(#name, #entry));
        }
    });

    // `use ...::DirectlyMutable` is needed to allow for the `add_entry` call.
    TokenStream::from(quote! {
        {
            use #directory_mod::helper::DirectlyMutable;

            #constructor

            #( #entries )*

            #result
        }
    })
}

struct DirectoryEntry {
    name: proc_macro2::TokenStream,
    entry: Expr,
}

impl Parse for DirectoryEntry {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        let mut seen = HashSet::new();
        let mut check_literal = |name: String, span| {
            if name.len() as u64 > MAX_FILENAME {
                let message = format!(
                    "Entry name is too long: '{}'\n\
                     Max entry name is {} bytes.\n\
                     This entry is {} bytes.",
                    name,
                    name.len(),
                    MAX_FILENAME
                );
                return Err(syn::Error::new(span, message));
            }

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
            // Can not use `input.lookahead()` here, as the third thing we expect is a complete
            // expression, and `lookahead()` only allows for tokens, as it is also building a nice
            // error message.  So we will construct an error message "manually" in the last branch.
            if input.peek(LitStr) {
                let str_lit = input.parse::<LitStr>()?;
                check_literal(str_lit.value(), str_lit.span())?;
                str_lit.into_token_stream()
            } else if input.peek(LitByteStr) {
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
    assign_to: Option<Ident>,
    entries: Vec<DirectoryEntry>,
}

impl Parse for PseudoDirectory {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        let assign_to = if input.peek(Ident) && input.peek2(Token![->]) {
            let ident = input.parse::<Ident>()?;
            input.parse::<Token![->]>()?;
            Some(ident)
        } else {
            None
        };

        let entries: Vec<DirectoryEntry> =
            input.parse_terminated::<_, Token![,]>(DirectoryEntry::parse)?.into_iter().collect();

        Ok(PseudoDirectory { assign_to, entries })
    }
}
