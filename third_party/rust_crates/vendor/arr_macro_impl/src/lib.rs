#![forbid(unsafe_code)]

extern crate proc_macro;

use proc_macro::TokenStream;
use proc_macro_hack::proc_macro_hack;
use syn::parse::Parse;
use syn::parse::ParseStream;
use syn::parse::Result;
use syn::{Token, parse_macro_input};

use quote::quote;
use syn::Expr;
use syn::LitInt;

struct ArrayInit {
    value: Expr,
    quantity: LitInt,
}

impl Parse for ArrayInit {
    fn parse(input: ParseStream) -> Result<Self> {
        let value: Expr = input.parse()?;
        input.parse::<Token![;]>()?;
        let quantity: LitInt = input.parse()?;
        Ok(ArrayInit {
            value,
            quantity,
        })
    }
}

#[proc_macro_hack]
pub fn arr(input: TokenStream) -> TokenStream {
    let ArrayInit {
        value,
        quantity
    } = parse_macro_input!(input as ArrayInit);

    let iter = std::iter::repeat(value).take(quantity.value() as usize);

    let result = quote! {
        [#(#iter),*]
    };
    result.into()
}
