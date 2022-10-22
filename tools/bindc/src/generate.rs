// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use bind::compiler;
use bind::linter;
use bind::parser::bind_library;
use bind::parser::common::Include;
use std::collections::HashSet;
use std::convert::TryFrom;

use crate::cpp_generator;
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

fn generate_declaration_name(name: &str, value: &bind_library::Value) -> String {
    match value {
        bind_library::Value::Number(value_name, _) => {
            format!("{}_{}", name, value_name)
        }
        bind_library::Value::Str(value_name, _) => {
            format!("{}_{}", name, value_name)
        }
        bind_library::Value::Bool(value_name, _) => {
            format!("{}_{}", name, value_name)
        }
        bind_library::Value::Enum(value_name) => {
            format!("{}_{}", name, value_name)
        }
    }
    .to_uppercase()
}

/// The generated identifiers for each value must be unique. Since the key and value identifiers
/// are joined using underscores which are also valid to use in the identifiers themselves,
/// duplicate keys may be produced. I.e. the key-value pair "A_B" and "C", and the key-value pair
/// "A" and "B_C", will both produce the identifier "A_B_C". This function hence ensures none of the
/// generated names are duplicates.
fn check_names(declarations: &Vec<bind_library::Declaration>) -> Result<(), Error> {
    let mut names: HashSet<String> = HashSet::new();
    let mut keys: HashSet<String> = HashSet::new();

    // Check key values.
    for declaration in declarations.into_iter() {
        // Check if there is a duplicate key name.
        let fidl_key_name = declaration.identifier.name.to_uppercase();
        if keys.contains(&fidl_key_name) {
            return Err(anyhow!("Name \"{}\" generated for more than one key", fidl_key_name));
        }
        keys.insert(fidl_key_name);

        for value in &declaration.values {
            let name = generate_declaration_name(&declaration.identifier.name, value);

            // Return an error if there is a duplicate name.
            if names.contains(&name) {
                return Err(anyhow!("Name \"{}\" generated for more than one key", name));
            }

            names.insert(name);
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn get_test_generated_cpp(input: &str) -> Vec<String> {
        generate(GeneratedBindingType::Cpp, input, false)
            .unwrap()
            .split("\n")
            .map(|s| s.to_string())
            .filter(|x| !x.is_empty())
            .collect()
    }

    fn get_test_generated_rust(input: &str) -> Vec<String> {
        generate(GeneratedBindingType::Rust, input, false)
            .unwrap()
            .split("\n")
            .map(|s| s.to_string())
            .filter(|x| !x.is_empty())
            .collect()
    }

    #[test]
    fn zero_keys() {
        let generated_cpp: Vec<String> = get_test_generated_cpp("library fuchsia.platform;");
        let generated_rust: Vec<String> = get_test_generated_rust("library fuchsia.platform;");

        let expected_cpp = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "#ifndef BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#define BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#include <string>".to_string(),
            "namespace bind_fuchsia_platform {".to_string(),
            "}  // namespace bind_fuchsia_platform".to_string(),
            "#endif  // BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
        ];
        let expected_rust = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
        ];

        assert!(generated_cpp.into_iter().zip(expected_cpp).all(|(a, b)| (a == b)));
        assert!(generated_rust.into_iter().zip(expected_rust).all(|(a, b)| (a == b)));
    }

    #[test]
    fn one_key() {
        let test_str = "library fuchsia.platform;\n
            string A_KEY {\n
                A_VALUE = \"a string value\",\n
            };";
        let generated_cpp: Vec<String> = get_test_generated_cpp(test_str);
        let generated_rust: Vec<String> = get_test_generated_rust(test_str);

        let expected_cpp = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "#ifndef BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#define BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#include <string>".to_string(),
            "namespace bind_fuchsia_platform {".to_string(),
            "static const std::string A_KEY = \"fuchsia.platform.A_KEY\";".to_string(),
            "static const std::string A_KEY_A_VALUE = \"a string value\";".to_string(),
            "}  // namespace bind_fuchsia_platform".to_string(),
            "#endif  // BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
        ];
        let expected_rust = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "pub const A_KEY: &str = \"fuchsia.platform.A_KEY\";".to_string(),
            "pub const A_KEY_A_VALUE: &str = \"a string value\";".to_string(),
        ];

        assert!(generated_cpp.into_iter().zip(expected_cpp).all(|(a, b)| (a == b)));
        assert!(generated_rust.into_iter().zip(expected_rust).all(|(a, b)| (a == b)));
    }

    #[test]
    fn one_key_extends() {
        let test_str = "library fuchsia.platform;\n
            extend uint fuchsia.BIND_PROTOCOL {\n
                BUS = 84,\n
            };";
        let generated_cpp: Vec<String> = get_test_generated_cpp(test_str);
        let generated_rust: Vec<String> = get_test_generated_rust(test_str);

        let expected_cpp = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "#ifndef BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#define BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#include <string>".to_string(),
            "namespace bind_fuchsia_platform {".to_string(),
            "static constexpr uint32_t BIND_PROTOCOL_BUS = 84;".to_string(),
            "}  // namespace bind_fuchsia_platform".to_string(),
            "#endif  // BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
        ];
        let expected_rust = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "pub const BIND_PROTOCOL_BUS: u32 = 84;".to_string(),
        ];

        assert!(generated_cpp.into_iter().zip(expected_cpp).all(|(a, b)| (a == b)));
        assert!(generated_rust.into_iter().zip(expected_rust).all(|(a, b)| (a == b)));
    }

    #[test]
    fn one_key_with_using() {
        let test_str = "library fuchsia.platform;\n
            using another.bindlibrary as another;
            extend uint another.SOME_INT {\n
                BUS = 84,\n
            };";
        let generated_cpp: Vec<String> = get_test_generated_cpp(test_str);
        let generated_rust: Vec<String> = get_test_generated_rust(test_str);

        let expected_cpp = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "#ifndef BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#define BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#include <string>".to_string(),
            "#include <bind/another/bindlibrary/cpp/bind.h>".to_string(),
            "namespace bind_fuchsia_platform {".to_string(),
            "static constexpr uint32_t SOME_INT_BUS = 84;".to_string(),
            "}  // namespace bind_fuchsia_platform".to_string(),
            "#endif  // BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
        ];
        let expected_rust = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "pub use bind_another_bindlibrary;".to_string(),
            "pub const SOME_INT_BUS: u32 = 84;".to_string(),
        ];

        assert!(generated_cpp.into_iter().zip(expected_cpp).all(|(a, b)| (a == b)));
        assert!(generated_rust.into_iter().zip(expected_rust).all(|(a, b)| (a == b)));
    }

    #[test]
    fn lower_snake_case() {
        let test_str =
            "library fuchsia.platform;\nstring a_key {\na_value = \"a string value\",\n};";
        let generated_cpp: Vec<String> = get_test_generated_cpp(test_str);
        let generated_rust: Vec<String> = get_test_generated_rust(test_str);

        let expected_cpp = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "#ifndef BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#define BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
            "#include <string>".to_string(),
            "namespace bind_fuchsia_platform {".to_string(),
            "static const std::string A_KEY = \"fuchsia.platform.a_key\";".to_string(),
            "static const std::string A_KEY_A_VALUE = \"a string value\";".to_string(),
            "}  // namespace bind_fuchsia_platform".to_string(),
            "#endif  // BIND_FUCHSIA_PLATFORM_BINDLIB_".to_string(),
        ];
        let expected_rust = vec![
            "// Copyright 2022 The Fuchsia Authors. All rights reserved.".to_string(),
            "// Use of this source code is governed by a BSD-style license that can be".to_string(),
            "// found in the LICENSE file.".to_string(),
            "// WARNING: This file is machine generated by bindc.".to_string(),
            "pub const A_KEY: &str = \"fuchsia.platform.a_key\";".to_string(),
            "pub const A_KEY_A_VALUE: &str = \"a string value\";".to_string(),
        ];

        assert!(generated_cpp.into_iter().zip(expected_cpp).all(|(a, b)| (a == b)));
        assert!(generated_rust.into_iter().zip(expected_rust).all(|(a, b)| (a == b)));
    }

    #[test]
    fn duplicate_key_value() {
        let test_str = "library fuchsia.platform;\n
            string A_KEY {\n
                A_VALUE = \"a string value\",\n
            };\n
            string A_KEY_A {\n
                VALUE = \"a string value\",\n
            };";
        assert!(generate(GeneratedBindingType::Cpp, test_str, false).is_err());
        assert!(generate(GeneratedBindingType::Rust, test_str, false).is_err());
    }

    #[test]
    fn duplicate_keys() {
        let test_str = "library fuchsia.platform;\n
            string A_KEY {\n
                A_VALUE = \"a string value\",\n
            };\n
            string A_KEY {\n
                VALUE = \"a string value\",\n
            };";
        assert!(generate(GeneratedBindingType::Cpp, test_str, false).is_err());
        assert!(generate(GeneratedBindingType::Rust, test_str, false).is_err());
    }

    #[test]
    fn duplicate_keys_mixed_cases() {
        let test_str = "library fuchsia.platform;\n
            string A_KEY {\n
                A_VALUE = \"a string value\",\n
            };\n
            string a_key {\n
                VALUE = \"a string value\",\n
            };";
        assert!(generate(GeneratedBindingType::Cpp, test_str, false).is_err());
        assert!(generate(GeneratedBindingType::Rust, test_str, false).is_err());
    }

    #[test]
    fn duplicate_values_in_a_key() {
        let test_str = "library fuchsia.platform;\n
            string A_KEY {\n
                A_VALUE = \"a string value\",\n
                A_VALUE = \"a string value\",\n
            };";
        assert!(generate(GeneratedBindingType::Cpp, test_str, false).is_err());
        assert!(generate(GeneratedBindingType::Rust, test_str, false).is_err());
    }

    #[test]
    fn duplicate_values_two_keys() {
        let test_str = "library fuchsia.platform;\n
            string KEY {\n
                A_VALUE = \"a string value\",\n
            };\n
            string KEY_A {\n
                VALUE = \"a string value\",\n
            };\n";
        assert!(generate(GeneratedBindingType::Cpp, test_str, false).is_err());
        assert!(generate(GeneratedBindingType::Rust, test_str, false).is_err());
    }

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

    #[test]
    fn test_deprecated_lib_cpp_generation() {
        assert_eq!(
            include_str!("tests/expected_deprecated_bind_cpp_header"),
            generate(
                GeneratedBindingType::Cpp,
                include_str!("../../../src/devices/bind/fuchsia/fuchsia.bind"),
                false
            )
            .unwrap()
        );
    }
}
