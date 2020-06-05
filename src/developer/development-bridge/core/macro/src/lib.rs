// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    proc_macro::TokenStream,
    proc_macro2::Span,
    quote::quote,
    syn::{
        parse_macro_input, punctuated::Punctuated, FnArg, Ident, ItemFn, ItemStruct, Pat, PatType,
        Token, Type::Path, TypePath,
    },
};

const EXPECTED_SIGNATURE: &str = "ffx_plugin expects one of the following signatures:\n\
(RemoteControlProxy, <YourArghCommand>) -> Result<(), anyhow::Error>\n\
OR\n\
(DaemonProxy, <YourArghCommand>) -> Result<(), anyhow::Error>\n\
OR\n\
(<YourArghCommand>) -> Result<(), anyhow::Error>";

#[proc_macro_attribute]
pub fn ffx_command(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input = parse_macro_input!(item as ItemStruct);
    let cmd = input.ident.clone();
    TokenStream::from(quote! {
        #input
        pub type FfxPluginCommand = #cmd;
    })
}

fn handle_daemon_remote_plugins(input: ItemFn) -> TokenStream {
    // Error check that the first parameter is RCS or DaemonProxy
    let mut is_remote = false;
    let proxy = input.sig.inputs[0].clone();
    match proxy {
        FnArg::Typed(PatType { ty, .. }) => match ty.as_ref() {
            Path(TypePath { path, .. }) => match path.segments.last() {
                Some(t) => {
                    if t.ident == Ident::new("RemoteControlProxy", Span::call_site()) {
                        is_remote = true;
                    } else if t.ident != Ident::new("DaemonProxy", Span::call_site()) {
                        panic!("{}", EXPECTED_SIGNATURE)
                    }
                }
                _ => panic!("{}", EXPECTED_SIGNATURE),
            },
            _ => panic!("{}", EXPECTED_SIGNATURE),
        },
        _ => panic!("{}", EXPECTED_SIGNATURE),
    }

    let mut args: Punctuated<Ident, Token!(,)> = Punctuated::new();
    let command = input.sig.inputs[1].clone();
    match command {
        FnArg::Typed(PatType { pat, .. }) => match pat.as_ref() {
            Pat::Ident(pat) => {
                if pat.subpat.is_some() {
                    panic!("{}", EXPECTED_SIGNATURE);
                } else {
                    args.push(pat.ident.clone());
                }
            }
            _ => panic!("{}", EXPECTED_SIGNATURE),
        },
        _ => panic!("{}", EXPECTED_SIGNATURE),
    }

    let method = input.sig.ident.clone();
    let cmd = input.sig.inputs[1].clone();
    if is_remote {
        TokenStream::from(quote! {
            #input
            pub async fn ffx_plugin_impl<D, R, DFut, RFut>(_daemon_factory: D, remote_factory: R, #cmd) -> Result<(), Error>
                where
                    D: FnOnce() -> DFut,
                    DFut: std::future::Future<Output = std::result::Result<fidl_fuchsia_developer_bridge::DaemonProxy, anyhow::Error>>,
                    R: FnOnce() -> RFut,
                    RFut: std::future::Future<Output = std::result::Result<fidl_fuchsia_developer_remotecontrol::RemoteControlProxy, anyhow::Error>>
            {
                #method(remote_factory().await?, #args).await
            }
        })
    } else {
        TokenStream::from(quote! {
            #input
            pub async fn ffx_plugin_impl<D, DFut, R, RFut>(daemon_factory: D, _remote_factory: R, #cmd) -> Result<(), Error>
                where
                    D: FnOnce() -> DFut,
                    DFut: std::future::Future<Output = std::result::Result<fidl_fuchsia_developer_bridge::DaemonProxy, anyhow::Error>>,
                    R: FnOnce() -> RFut,
                    RFut: std::future::Future<Output = std::result::Result<fidl_fuchsia_developer_remotecontrol::RemoteControlProxy, anyhow::Error>>
            {
                #method(daemon_factory().await?, #args).await
            }
        })
    }
}

fn handle_independent_plugins(input: ItemFn) -> TokenStream {
    let method = input.sig.ident.clone();
    let inputs = input.sig.inputs.clone();
    let mut args: Punctuated<Ident, Token!(,)> = Punctuated::new();
    input.sig.inputs.iter().for_each(|arg| match arg {
        FnArg::Receiver(_) => panic!("ffx plugin method signature cannot contain self"),
        FnArg::Typed(PatType { pat, .. }) => match pat.as_ref() {
            Pat::Ident(pat) => {
                if pat.subpat.is_some() {
                    panic!("{}", EXPECTED_SIGNATURE);
                } else {
                    args.push(pat.ident.clone());
                }
            }
            _ => {
                panic!("{}", EXPECTED_SIGNATURE);
            }
        },
    });

    TokenStream::from(quote! {
        #input
        pub async fn ffx_plugin_impl<D, R, DFut, RFut>(_daemon_factory: D, _remote_factory: R, #inputs) -> Result<(), Error>
            where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<Output = std::result::Result<fidl_fuchsia_developer_bridge::DaemonProxy, anyhow::Error>>,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<Output = std::result::Result<fidl_fuchsia_developer_remotecontrol::RemoteControlProxy, anyhow::Error>>
        {
            #method(#args).await
        }
    })
}

///TODO(fxb/51915): Accept a FIDL string marker and generate the code to get the FIDL proxy from
///the RCS so 3rd party devs don't need to know about the RCS.
#[proc_macro_attribute]
pub fn ffx_plugin(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input = parse_macro_input!(item as ItemFn);

    if input.sig.inputs.len() == 2 {
        handle_daemon_remote_plugins(input)
    } else if input.sig.inputs.len() == 1 {
        handle_independent_plugins(input)
    } else {
        panic!("{}", EXPECTED_SIGNATURE);
    }
}
