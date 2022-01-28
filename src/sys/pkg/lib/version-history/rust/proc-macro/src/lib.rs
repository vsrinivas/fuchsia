// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {proc_macro::TokenStream, version_history_shared::version_history};

#[proc_macro]
pub fn declare_version_history(_tokens: TokenStream) -> TokenStream {
    let versions = version_history().expect("version-history.json to be parsed");

    let mut tokens = String::from("[");
    for version in versions {
        tokens.push_str(&format!(
            "Version {{ api_level: {}, abi_revision: {} }},",
            version.api_level, version.abi_revision
        ));
    }
    tokens.push_str("]");

    tokens.parse().unwrap()
}
