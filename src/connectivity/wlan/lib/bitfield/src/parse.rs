// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    proc_macro2::TokenStream,
    syn::{
        braced,
        export::ToTokens,
        parenthesized,
        parse::{Parse, ParseStream},
        punctuated::Punctuated,
        token, Ident, LitInt, Path, Result, Token, Type,
    },
};

pub enum BitRange {
    Closed { start_inclusive: LitInt, separator: Token![..=], end_inclusive: LitInt },
    SingleBit { index: LitInt },
}

impl Parse for BitRange {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
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

pub struct Alias {
    pub name: Ident,
    pub user_type: Option<UserType>,
}

impl Parse for Alias {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        let name = input.parse()?;
        let lookahead = input.lookahead1();
        let user_type = if lookahead.peek(Token![as]) { Some(input.parse()?) } else { None };
        Ok(Alias { name, user_type })
    }
}

pub struct UserType {
    pub as_keyword: Token![as],
    pub type_name: Path,
    pub paren: token::Paren,
    pub inner_int_type: Type,
}

impl Parse for UserType {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        let inside_parens;
        Ok(UserType {
            as_keyword: input.parse()?,
            type_name: input.parse()?,
            paren: parenthesized!(inside_parens in input),
            inner_int_type: inside_parens.parse()?,
        })
    }
}

pub enum AliasSpec {
    Unnamed(Token![_]),
    SingleName(Alias),
    Union {
        union_keyword: Token![union],
        brace: token::Brace,
        aliases: Punctuated<Alias, Token![,]>,
    },
}

impl AliasSpec {
    pub fn all_aliases(&self) -> Vec<&Alias> {
        match self {
            AliasSpec::Unnamed(_) => vec![],
            AliasSpec::SingleName(name) => vec![name],
            AliasSpec::Union { aliases, .. } => aliases.iter().collect(),
        }
    }

    pub fn first_name(&self) -> String {
        match self {
            AliasSpec::Unnamed(_) => "_".to_string(),
            AliasSpec::SingleName(alias) => alias.name.to_string(),
            AliasSpec::Union { aliases, .. } => match aliases.first() {
                None => "_".to_string(),
                Some(alias) => alias.name.to_string(),
            },
        }
    }
}

impl Parse for AliasSpec {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        let lookahead = input.lookahead1();
        Ok(if lookahead.peek(Token![_]) {
            AliasSpec::Unnamed(input.parse()?)
        } else if lookahead.peek(Token![union]) {
            let brace_contents;
            AliasSpec::Union {
                union_keyword: input.parse()?,
                brace: braced!(brace_contents in input),
                aliases: brace_contents.parse_terminated(Alias::parse)?,
            }
        } else {
            AliasSpec::SingleName(input.parse()?)
        })
    }
}

pub struct FieldDef {
    pub bits: BitRange,
    pub aliases: AliasSpec,
}

impl Parse for FieldDef {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        Ok(FieldDef { bits: input.parse()?, aliases: input.parse()? })
    }
}

pub struct FieldList {
    pub fields: Punctuated<FieldDef, Token![,]>,
}

impl Parse for FieldList {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        Ok(FieldList { fields: input.parse_terminated(FieldDef::parse)? })
    }
}
