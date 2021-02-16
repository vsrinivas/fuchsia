// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ast::*, heck::SnakeCase, std::iter};

pub fn to_c_name(name: &str) -> String {
    if name.is_empty() {
        name.to_string()
    } else {
        // strip FQN
        let name = name.split(".").last().unwrap();
        // Force uppercase characters the follow one another to lowercase.
        // e.g. GUIDType becomes Guidtype
        let mut iter = name.chars();
        let name = iter::once(iter.next().unwrap())
            .chain(iter.zip(name.chars()).map(|(c1, c2)| {
                if c2.is_ascii_uppercase() {
                    c1.to_ascii_lowercase()
                } else {
                    c1
                }
            }))
            .collect::<String>();
        name.trim().to_snake_case()
    }
}

pub fn is_banjo_namespace(name: &str) -> bool {
    let banjo_namespaces = vec!["banjo.examples", "ddk.hw", "fuchsia.hardware", "fuchsia.sysmem"];
    banjo_namespaces.iter().any(|n| name.contains(n))
}

pub fn get_size_spec<'b>(ast: &'b BanjoAst, size: &'b str) -> &'b str {
    // Check if the size specification is a constant and replace the constant name by its value
    // if that's the case.
    // This matches FIDL's behavior of inlining constant values in array sizes.
    let size_ident = Ident::new_raw(size);
    if let Some(size_decl) = ast.maybe_id_to_decl(&size_ident) {
        if let Decl::Constant { value, .. } = size_decl {
            return &value.0;
        }
    }
    size
}
