// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
pub(crate) fn parse_macro_derive(input: &str) -> syn::DeriveInput {
    syn::parse_str(input).unwrap()
}
