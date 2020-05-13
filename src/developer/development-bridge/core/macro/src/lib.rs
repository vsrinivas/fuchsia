// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    proc_macro::TokenStream,
    quote::quote,
    syn::{
        parse_macro_input, punctuated::Punctuated, FnArg, Ident, ItemFn, ItemStruct, Pat, PatType,
        Token,
    },
};

#[proc_macro_attribute]
pub fn ffx_command(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input = parse_macro_input!(item as ItemStruct);
    let cmd = input.ident.clone();
    TokenStream::from(quote! {
        #input
        pub type FfxPluginCommand = #cmd;
    })
}

///TODO(fxb/51915): Accept a FIDL string marker and generate the code to get the FIDL proxy from
///the RCS so 3rd party devs don't need to know about the RCS.
#[proc_macro_attribute]
pub fn ffx_plugin(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input = parse_macro_input!(item as ItemFn);
    let method = input.sig.ident.clone();
    let inputs = input.sig.inputs.clone();
    let mut args: Punctuated<Ident, Token!(,)> = Punctuated::new();
    input.sig.inputs.iter().for_each(|arg| match arg {
        FnArg::Receiver(_) => panic!("ffx plugin method signature cannot contain self"),
        FnArg::Typed(PatType { pat, .. }) => match pat.as_ref() {
            Pat::Ident(pat) => {
                if pat.subpat.is_some() {
                    panic!("ffx plugin cannot contain attribute arguments");
                } else {
                    args.push(pat.ident.clone());
                }
            }
            _ => {
                panic!("ffx plugin does not support patterns in function arguments");
            }
        },
    });

    TokenStream::from(quote! {
        #input
        pub async fn ffx_plugin_impl(#inputs) -> Result<(), Error> {
            #method(#args).await
        }
    })
}
