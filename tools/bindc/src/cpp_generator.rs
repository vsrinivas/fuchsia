// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bind::parser::common::Include;

use crate::generate::BindingGenerator;

pub struct CppGenerator {}

impl BindingGenerator for CppGenerator {
    fn generate_using_declaration(self: &Self, using_decl: &Include) -> String {
        let include_stem = using_decl.name.to_string().replace(".", "/");
        format!("#include <bind/{}/cpp/bind.h>\n", include_stem)
    }

    fn generate_identifier_declaration(self: &Self, path: &str, identifier_name: &str) -> String {
        let mut var_name = identifier_name.to_uppercase();

        // Remove the BIND_ prefix for the variable name if the identifier is from the fuchsia
        // base library. This is to prevent conflicts with the macros defined in binding_priv.h.
        if path == "fuchsia" {
            var_name = var_name.strip_prefix("BIND_").unwrap_or(&var_name).to_string();
        }

        format!("static const std::string {} = \"{}.{}\";\n", var_name, path, identifier_name)
    }

    fn generate_numerical_value_declaration(self: &Self, name: &str, val: &u64) -> String {
        format!("static constexpr uint32_t {} = {};\n", name, val)
    }

    fn generate_string_value_declaration(self: &Self, name: &str, val: &str) -> String {
        format!("static const std::string {} = \"{}\";\n", name, val)
    }

    fn generate_bool_value_declaration(self: &Self, name: &str, val: &bool) -> String {
        format!("static constexpr bool {} = {};\n", name, val)
    }

    fn generate_enum_value_declaration(
        self: &Self,
        name: &str,
        path: &str,
        identifier_name: &str,
        val: &str,
    ) -> String {
        format!("static const std::string {} = \"{}.{}.{}\";\n", name, path, identifier_name, val)
    }

    fn generate_result(
        self: &Self,
        bind_name: &str,
        using_declarations: &str,
        constant_declarations: &str,
    ) -> String {
        let namespace = format!("bind_{}", bind_name.replace(".", "_"));
        let header_guard = format!("{}_BINDLIB_", namespace.to_uppercase());
        format!(
            include_str!("templates/cpp_header.template"),
            namespace = namespace,
            header_guard = header_guard,
            dep_includes = using_declarations,
            content = constant_declarations,
        )
    }
}
