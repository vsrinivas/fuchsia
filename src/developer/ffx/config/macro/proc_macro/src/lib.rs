// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {proc_macro::TokenStream, proc_macro_hack::proc_macro_hack, serde_json::Value, std::env};

#[proc_macro_hack]
pub fn include_default(_input: TokenStream) -> TokenStream {
    // Test deserializing the default configuration file at compile time.
    let default = include_str!(env!("FFX_DEFAULT_CONFIG_JSON"));
    // This is being used as a way of validating the default json.
    // This should not happen as the JSON file is built using json_merge which does validation, but
    // leaving this here as the last line of defense.
    let _res: Option<Value> =
        serde_json::from_str(default).expect("default configuration is malformed");

    std::format!("Some(serde_json::json!({}))", default).parse().unwrap()
}
