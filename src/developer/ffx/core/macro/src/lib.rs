// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ffx_core_impl::ProxyMap,
    proc_macro::TokenStream,
    syn::{parse_macro_input, ItemFn, ItemStruct},
};

#[proc_macro_attribute]
pub fn ffx_command(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input = parse_macro_input!(item as ItemStruct);
    TokenStream::from(ffx_core_impl::ffx_command(input))
}

#[proc_macro_attribute]
pub fn ffx_plugin(attr: TokenStream, item: TokenStream) -> TokenStream {
    let input = parse_macro_input!(item as ItemFn);
    let proxies = parse_macro_input!(attr as ProxyMap);
    ffx_core_impl::ffx_plugin(input, proxies).unwrap_or_else(|err| err.to_compile_error()).into()
}
