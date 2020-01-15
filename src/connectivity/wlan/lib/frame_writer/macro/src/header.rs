// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::frame_writer::BufferWrite,
    quote::quote,
    syn::{
        parse::{Parse, ParseStream},
        spanned::Spanned,
        Error, Expr, ExprIf, ExprPath, Path, Result, Token,
    },
};

pub struct HeaderDefinition {
    header_type: Path,
    optional: bool,
    value: Expr,
}

impl BufferWrite for HeaderDefinition {
    fn gen_frame_len_tokens(&self) -> Result<proc_macro2::TokenStream> {
        let header_type = &self.header_type;
        Ok(match &self.value {
            Expr::If(ExprIf { cond, else_branch, .. }) => {
                if let Some(_) = else_branch {
                    quote!(frame_len += std::mem::size_of::<#header_type>();)
                } else {
                    quote!(if #cond { frame_len += std::mem::size_of::<#header_type>(); })
                }
            }
            Expr::Path(path) => {
                if self.optional {
                    quote!(if #path.is_some() { frame_len += std::mem::size_of::<#header_type>(); })
                } else {
                    quote!(frame_len += std::mem::size_of::<#header_type>();)
                }
            }
            _ => quote!(frame_len += std::mem::size_of::<#header_type>();),
        })
    }

    fn gen_write_to_buf_tokens(&self) -> Result<proc_macro2::TokenStream> {
        let header_type = &self.header_type;
        Ok(match &self.value {
            Expr::If(ExprIf { cond, then_branch, else_branch, .. }) => {
                if let Some((_, else_branch)) = else_branch {
                    quote!(w.append_value(if #cond { #then_branch } else { #else_branch } as &#header_type)?;)
                } else {
                    quote!(if #cond { w.append_value(#then_branch as &#header_type)?; })
                }
            }
            Expr::Path(path) => {
                if self.optional {
                    quote!(if let Some(v) = #path.as_ref() { w.append_value(v as &#header_type)?; })
                } else {
                    quote!(w.append_value(#path as &#header_type)?;)
                }
            }
            other => quote!(w.append_value(#other as &#header_type)?;),
        })
    }
    fn gen_var_declaration_tokens(&self) -> Result<proc_macro2::TokenStream> {
        Ok(quote!())
    }
}

impl Parse for HeaderDefinition {
    fn parse(input: ParseStream) -> Result<Self> {
        let header_type = input.parse::<ExprPath>()?.path;

        let optional = input.peek(Token![?]);
        if optional {
            input.parse::<Token![?]>()?;
        }
        input.parse::<Token![:]>()?;

        let value = input.parse::<Expr>()?;
        match value {
            Expr::If(_)
            | Expr::Path(_)
            | Expr::Block(_)
            | Expr::Call(_)
            | Expr::Lit(_)
            | Expr::MethodCall(_)
            | Expr::Reference(_)
            | Expr::Repeat(_)
            | Expr::Struct(_)
            | Expr::Tuple(_)
            | Expr::Unary(_) => (),
            _ => return Err(Error::new(header_type.span(), "invalid expression for header value")),
        }

        return Ok(HeaderDefinition { optional, header_type, value });
    }
}
