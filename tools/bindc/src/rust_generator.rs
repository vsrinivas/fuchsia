// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bind::parser::common::Include;

use crate::generate::BindingGenerator;

pub struct RustGenerator {}

impl BindingGenerator for RustGenerator {
    fn generate_using_declaration(self: &Self, using_decl: &Include) -> String {
        let dep_name_underscores = using_decl.name.to_string().replace(".", "_");
        format!("pub use {}_bindlib;\n", dep_name_underscores)
    }

    fn generate_identifier_declaration(self: &Self, path: &str, identifier_name: &str) -> String {
        format!(
            "pub const {}: &str = \"{}.{}\";\n",
            identifier_name.to_uppercase(),
            path,
            identifier_name
        )
    }

    fn generate_numerical_value_declaration(self: &Self, name: &str, val: &u64) -> String {
        format!("pub const {}: u32 = {};\n", name, val)
    }

    fn generate_string_value_declaration(self: &Self, name: &str, val: &str) -> String {
        format!("pub const {}: &str = \"{}\";\n", name, val)
    }

    fn generate_bool_value_declaration(self: &Self, name: &str, val: &bool) -> String {
        format!("pub const {}: bool = {};\n", name, val)
    }

    fn generate_enum_value_declaration(
        self: &Self,
        name: &str,
        path: &str,
        identifier_name: &str,
        val: &str,
    ) -> String {
        format!("pub const {}: &str = \"{}.{}.{}\";\n", name, path, identifier_name, val)
    }

    fn generate_result(
        self: &Self,
        _bind_name: &str,
        using_declarations: &str,
        constant_declarations: &str,
    ) -> String {
        format!(
            include_str!("templates/rust_file.template"),
            expose = using_declarations,
            content = constant_declarations
        )
    }
}
