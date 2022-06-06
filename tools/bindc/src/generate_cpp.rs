// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use bind::compiler;
use bind::linter;
use bind::parser::bind_library;
use std::convert::TryFrom;
use std::fs::File;
use std::io::{self, Write as IoWrite};
use std::path::PathBuf;

use crate::check_names;
use crate::generate_declaration_name;
use crate::read_file;

pub fn handle_generate_cpp(
    input: PathBuf,
    lint: bool,
    output: Option<PathBuf>,
) -> Result<(), Error> {
    let input_content = read_file(&input)?;

    // Generate the C++ header file.
    let generated_content = generate_cpp_header(&input_content, lint)?;

    // Create and open output file.
    let mut output_writer: Box<dyn io::Write> = if let Some(output) = output {
        Box::new(File::create(output).context("Failed to create output file.")?)
    } else {
        // Output file name was not given. Print result to stdout.
        Box::new(io::stdout())
    };

    // Write C++ header file to output.
    output_writer
        .write_all(generated_content.as_bytes())
        .context("Failed to write to output file")?;

    Ok(())
}

pub fn generate_cpp_header(input: &str, lint: bool) -> Result<String, Error> {
    let syntax_tree =
        bind_library::Ast::try_from(input).map_err(compiler::CompilerError::BindParserError)?;
    if lint {
        linter::lint_library(&syntax_tree).map_err(compiler::CompilerError::LinterError)?;
    }

    // Use the bind library name as the c++ header namespace.
    let bind_name = syntax_tree.name.to_string();
    let namespace = bind_name.replace(".", "_");
    let header_guard = format!("{}_BINDLIB_", namespace.to_uppercase());
    let dep_includes = syntax_tree
        .using
        .iter()
        .map(|using_statement| {
            let include_stem = using_statement.name.to_string().replace(".", "/");
            format!("#include <{}/bindlib.h>", include_stem)
        })
        .collect::<Vec<String>>()
        .join("\n");

    check_names(&syntax_tree.declarations)?;

    // Convert all key value pairs to their equivalent constants.
    let content = syntax_tree
        .declarations
        .into_iter()
        .map(|declaration| convert_to_cpp_constant(declaration, &bind_name))
        .collect::<Result<Vec<String>, _>>()?
        .join("\n");

    // Output result into template.
    Ok(format!(
        include_str!("templates/cpp_header.template"),
        namespace = namespace,
        header_guard = header_guard,
        content = content,
        dep_includes = dep_includes,
    ))
}

/// Converts a declaration to the cpp constant format.
fn convert_to_cpp_constant(
    declaration: bind_library::Declaration,
    path: &str,
) -> Result<String, Error> {
    let cpp_identifier = declaration.identifier.name.to_uppercase();
    let identifier_name = declaration.identifier.name;

    // Generating the key definition is only done when it is not extended.
    // When it is extended, the key will already be defined in the library that it is
    // extending from.
    let identifier_decl = if !declaration.extends {
        format!(
            "static const std::string {} = \"{}.{}\";\n",
            &cpp_identifier, path, &identifier_name
        )
    } else {
        format!("")
    };

    let value_decls = declaration
        .values
        .iter()
        .map(|value| {
            let name = generate_declaration_name(&cpp_identifier, value);
            match &value {
                bind_library::Value::Number(_, val) => {
                    format!("static constexpr uint32_t {} = {};\n", name, val)
                }
                bind_library::Value::Str(_, val) => {
                    format!("static const std::string {} = \"{}\";\n", name, val)
                }
                bind_library::Value::Bool(_, val) => {
                    format!("static constexpr bool {} = {};\n", name, val)
                }
                bind_library::Value::Enum(val) => {
                    format!(
                        "static const std::string {} = \"{}.{}.{}\";\n",
                        name, path, &identifier_name, val
                    )
                }
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
            generate_cpp_header(include_str!("tests/test_library.bind"), false).unwrap()
        );
    }
}
