// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {proc_macro::TokenStream, proc_macro_hack::proc_macro_hack, serde_json::Value};

#[proc_macro_hack]
pub fn include_default(_input: TokenStream) -> TokenStream {
    // Test deserializing the defaults.json file at compile time.
    let default = include_str!("../../../data/defaults.json");
    let _res: Option<Value> = serde_json::from_str(default).expect("defaults.json is malformed");

    std::format!("Some(serde_json::json!({}))", default).parse().unwrap()
}
