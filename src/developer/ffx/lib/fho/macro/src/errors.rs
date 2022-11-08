// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro2::{Span, TokenStream};
use quote::{quote_spanned, ToTokens};

#[derive(Debug)]
pub enum ParseError {
    MalformedFfxAttr(Span),
    DuplicateAttr(Span),
    DuplicateFfxAttr(Span),
    CommandRequired(Span),
    OnlyStructsSupported(Span),
    OnlyNamedFieldStructsSupported(Span),
    InvalidCheckAttr(Span),
}

impl ToTokens for ParseError {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        match self {
            ParseError::MalformedFfxAttr(span) => tokens.extend(
                quote_spanned! {*span=>
                    std::compile_error!("`#[ffx]` attribute malformed. Must contain a list of property literals. So far only `forces_stdout_logs` is supported.");
                }
            ),
            ParseError::DuplicateAttr(span) => tokens.extend(
                quote_spanned! {*span=>
                    std::compile_error!("Duplicate attribute found. Can only have one attribute kind per field");
                }
            ),
            ParseError::DuplicateFfxAttr(span) => tokens.extend(
                quote_spanned! {*span=>
                    std::compile_error!("Duplicate ffx attribute found. Can only have one attribute of each kind");
                }
            ),
            ParseError::CommandRequired(span) => tokens.extend(
                quote_spanned! {*span=>
                    std::compile_error!("An ffx tool must have exactly one field denoted with the `#[command]` attribute");
                }
            ),
            ParseError::OnlyStructsSupported(span) => tokens.extend(
                quote_spanned! {*span=>
                    std::compile_error!("`#[derive(FfxTool)]` can only be applied to structs.");
                }
            ),
            ParseError::OnlyNamedFieldStructsSupported(span) => tokens.extend(
                quote_spanned! {*span=>
                    std::compile_error!("`#[derive(FfxTool)]` does not support unit or tuple structs");
                }
            ),
            ParseError::InvalidCheckAttr(span) => tokens.extend(
                quote_spanned! {*span=>
                    std::compile_error!("`#[check()]` attribute contents must be a call expression");
                }
            ),
        }
    }
}
