// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use bind::compiler;
use bind::linter;
use bind::parser::bind_library;
use bind::parser::common::Include;
use std::convert::TryFrom;

use crate::check_names;
use crate::cpp_generator;
use crate::generate_declaration_name;
use crate::rust_generator;
use crate::GeneratedBindingType;

pub trait BindingGenerator {
    fn generate_using_declaration(self: &Self, using_decl: &Include) -> String;
    fn generate_identifier_declaration(self: &Self, path: &str, identifier_name: &str) -> String;
    fn generate_numerical_value_declaration(self: &Self, name: &str, val: &u64) -> String;
    fn generate_string_value_declaration(self: &Self, name: &str, val: &str) -> String;
    fn generate_bool_value_declaration(self: &Self, name: &str, val: &bool) -> String;
    fn generate_enum_value_declaration(
        self: &Self,
        name: &str,
        path: &str,
        identifier_name: &str,
        val: &str,
    ) -> String;
    fn generate_result(
        self: &Self,
        bind_name: &str,
        using_declarations: &str,
        constant_declarations: &str,
    ) -> String;
}

pub fn generate(
    binding_type: GeneratedBindingType,
    input: &str,
    lint: bool,
) -> Result<String, Error> {
    let syntax_tree =
        bind_library::Ast::try_from(input).map_err(compiler::CompilerError::BindParserError)?;
    if lint {
        linter::lint_library(&syntax_tree).map_err(compiler::CompilerError::LinterError)?;
    }

    let binding_generator: Box<dyn BindingGenerator> = match binding_type {
        GeneratedBindingType::Cpp => Box::new(cpp_generator::CppGenerator {}),
        GeneratedBindingType::Rust => Box::new(rust_generator::RustGenerator {}),
    };

    // Put in dependencies from the library.
    let using_declarations = syntax_tree
        .using
        .iter()
        .map(|using_statement| binding_generator.generate_using_declaration(using_statement))
        .collect::<Vec<String>>()
        .join("");

    check_names(&syntax_tree.declarations)?;
    let bind_name = syntax_tree.name.to_string();

    // Convert all key value pairs to their equivalent constants.
    let constant_declarations = syntax_tree
        .declarations
        .into_iter()
        .map(|declaration| generate_to_constant(&binding_generator, declaration, &bind_name))
        .collect::<Result<Vec<String>, _>>()?
        .join("\n");

    let result =
        binding_generator.generate_result(&bind_name, &using_declarations, &constant_declarations);
    Ok(result)
}

fn generate_to_constant(
    binding_generator: &Box<dyn BindingGenerator>,
    declaration: bind_library::Declaration,
    path: &str,
) -> Result<String, Error> {
    let identifier_name = declaration.identifier.name;

    // Generating the key definition is only done when it is not extended.
    // When it is extended, the key will already be defined in the library that it is
    // extending from.
    let identifier_decl = if !declaration.extends {
        binding_generator.generate_identifier_declaration(path, &identifier_name)
    } else {
        format!("")
    };

    let value_decls = declaration
        .values
        .iter()
        .map(|value| {
            let name = generate_declaration_name(&identifier_name.to_uppercase(), value);
            match value {
                bind_library::Value::Number(_, val) => {
                    binding_generator.generate_numerical_value_declaration(&name, val)
                }
                bind_library::Value::Str(_, val) => {
                    binding_generator.generate_string_value_declaration(&name, val)
                }
                bind_library::Value::Bool(_, val) => {
                    binding_generator.generate_bool_value_declaration(&name, val)
                }
                bind_library::Value::Enum(val) => binding_generator
                    .generate_enum_value_declaration(&name, path, &identifier_name, val),
            }
        })
        .collect::<Vec<String>>()
        .join("");

    Ok(format!("{}{}", identifier_decl, value_decls))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_cpp_header_generation() {
        assert_eq!(
            include_str!("tests/expected_cpp_header_gen"),
            generate(GeneratedBindingType::Cpp, include_str!("tests/test_library.bind"), false)
                .unwrap()
        );
    }

    #[test]
    fn test_rust_file_generation() {
        assert_eq!(
            include_str!("tests/expected_rust_file_gen"),
            generate(GeneratedBindingType::Rust, include_str!("tests/test_library.bind"), false)
                .unwrap()
        );
    }
}
