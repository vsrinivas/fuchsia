// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    proc_macro2::TokenStream,
    syn::{
        parse::{Parse, ParseStream},
        punctuated::Punctuated,
        Ident, LitInt, Result, Token,
    },
    synstructure::ToTokens,
};

pub enum BitRange {
    Closed { start_inclusive: LitInt, separator: Token![..=], end_inclusive: LitInt },
    SingleBit { index: LitInt },
}

impl ToTokens for BitRange {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        match self {
            BitRange::Closed { start_inclusive, separator, end_inclusive } => {
                start_inclusive.to_tokens(tokens);
                separator.to_tokens(tokens);
                end_inclusive.to_tokens(tokens);
            }
            BitRange::SingleBit { index } => index.to_tokens(tokens),
        }
    }
}

pub enum IdentOrUnderscore {
    Ident(Ident),
    Underscore(Token![_]),
}

impl std::fmt::Display for IdentOrUnderscore {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            IdentOrUnderscore::Ident(ident) => ident.fmt(f),
            IdentOrUnderscore::Underscore(_) => write!(f, "_"),
        }
    }
}

impl Parse for IdentOrUnderscore {
    fn parse(input: ParseStream) -> Result<Self> {
        let lookahead = input.lookahead1();
        Ok(if lookahead.peek(Token![_]) {
            IdentOrUnderscore::Underscore(input.parse()?)
        } else {
            IdentOrUnderscore::Ident(input.parse()?)
        })
    }
}

pub struct FieldDef {
    pub bits: BitRange,
    pub name: IdentOrUnderscore,
}

pub struct FieldList {
    pub fields: Punctuated<FieldDef, Token![,]>,
}

impl Parse for BitRange {
    fn parse(input: ParseStream) -> Result<Self> {
        let start: LitInt = input.parse()?;
        let lookahead = input.lookahead1();
        if lookahead.peek(Token![..=]) {
            Ok(BitRange::Closed {
                start_inclusive: start,
                separator: input.parse()?,
                end_inclusive: input.parse()?,
            })
        } else {
            Ok(BitRange::SingleBit { index: start })
        }
    }
}

impl Parse for FieldDef {
    fn parse(input: ParseStream) -> Result<Self> {
        Ok(FieldDef { bits: input.parse()?, name: input.parse()? })
    }
}

impl Parse for FieldList {
    fn parse(input: ParseStream) -> Result<Self> {
        Ok(FieldList { fields: input.parse_terminated(FieldDef::parse)? })
    }
}
