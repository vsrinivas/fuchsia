// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use quote::quote;
use syn::{parse_macro_input, DeriveInput};

#[proc_macro_derive(FileDesc)]
pub fn derive_file_desc(item: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let name = parse_macro_input!(item as DeriveInput).ident;
    (quote! {
        impl ::std::ops::Deref for #name {
            type Target = crate::fs::FileCommon;
            fn deref(&self) -> &Self::Target {
                &self.common
            }
        }
    })
    .into()
}
