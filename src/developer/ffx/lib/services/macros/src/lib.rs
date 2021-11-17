// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {proc_macro::TokenStream, quote::quote};

#[proc_macro_attribute]
pub fn ffx_service(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input: syn::ItemStruct = syn::parse(item.into()).expect("expected struct");
    let name = input.ident.clone();
    // TODO(awdavies): Include things like generics and lifetime params.
    let q = quote! {
        #input

        pub type ServiceType = #name;
    };

    TokenStream::from(q)
}
