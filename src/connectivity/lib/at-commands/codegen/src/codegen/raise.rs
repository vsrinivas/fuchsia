// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module containing methods for generating code to "raise" from the low level
//! generic ASTs to the high level, typed, generated AT command and response types.

use {
    super::{
        common::{to_initial_capital, write_indent, write_newline, TABSTOP},
        error::Result,
    },
    crate::definition::{
        Argument, Arguments, Command, Definition, ExecuteArguments, PrimitiveType, Type,
    },
    std::io,
};

/// Entry point to generate `raise` methods at a given indentation.
pub fn codegen<W: io::Write>(sink: &mut W, indent: u64, definitions: &[Definition]) -> Result {
    codegen_commands(sink, indent, definitions)?;
    codegen_responses(sink, indent, definitions)
}

fn codegen_commands<W: io::Write>(sink: &mut W, indent: u64, definitions: &[Definition]) -> Result {
    write_indented!(sink, indent, "pub fn raise_command(lowlevel: &lowlevel::Command) -> Result<highlevel::Command, DeserializeError> {{\n")?;

    // Increment indent
    {
        let indent = indent + TABSTOP;

        write_indented!(sink, indent, "match lowlevel {{\n")?;

        // Increment indent
        {
            let indent = indent + TABSTOP;

            for definition in definitions {
                if let Definition::Command(command) = definition {
                    codegen_command(sink, indent, command)?;
                }
            }

            write_indented!(
                sink,
                indent,
                "_ => Err(DeserializeError::UnknownCommand(lowlevel.clone())),\n"
            )?;
        }

        write_indented!(sink, indent, "}}\n")?;
    }

    write_indented!(sink, indent, "}}\n\n")?;

    Ok(())
}

fn codegen_command<W: io::Write>(sink: &mut W, indent: u64, command: &Command) -> Result {
    match command {
        Command::Execute { name, is_extension, arguments } => {
            codegen_match_branch(
                sink,
                indent,
                "Command",
                "Execute",
                name,
                &command.type_name(),
                *is_extension,
                arguments.as_ref(),
            )?;
        }
        Command::Read { name, is_extension } => {
            codegen_match_branch(
                sink,
                indent,
                "Command",
                "Read",
                name,
                &command.type_name(),
                *is_extension,
                None::<&ExecuteArguments>,
            )?;
        }
        Command::Test { name, is_extension } => {
            codegen_match_branch(
                sink,
                indent,
                "Command",
                "Test",
                name,
                &command.type_name(),
                *is_extension,
                None::<&ExecuteArguments>,
            )?;
        }
    };
    write_newline(sink)?;

    Ok(())
}

fn codegen_responses<W: io::Write>(
    sink: &mut W,
    indent: u64,
    definitions: &[Definition],
) -> Result {
    write_indented!(sink, indent, "pub fn raise_response(lowlevel: &lowlevel::Response) -> Result<highlevel::Response, DeserializeError> {{\n")?;

    // Increment indent
    {
        let indent = indent + TABSTOP;

        write_indented!(sink, indent, "match lowlevel {{\n")?;

        // Increment indent
        {
            let indent = indent + TABSTOP;

            for definition in definitions {
                if let Definition::Response { name, is_extension, arguments } = definition {
                    let type_name = to_initial_capital(name);
                    codegen_match_branch(
                        sink,
                        indent,
                        "Response",
                        "Success",
                        name,
                        &type_name,
                        *is_extension,
                        Some(arguments),
                    )?;
                };
                write_newline(sink)?;
            }

            write_indented!(
                sink,
                indent,
                "_ => Err(DeserializeError::UnknownResponse(lowlevel.clone())),\n"
            )?;
        }

        write_indented!(sink, indent, "}}\n")?;
    }

    write_indented!(sink, indent, "}}\n\n")?;

    Ok(())
}

fn codegen_match_branch<W: io::Write, A: CodegenArguments>(
    sink: &mut W,
    indent: u64,
    typ: &str,
    variant: &str,
    name: &str,
    type_name: &str,
    is_extension: bool,
    arguments: Option<&A>,
) -> Result {
    write_indented!(
        sink,
        indent,
        "lowlevel::{}::{} {{ name, is_extension: {}",
        typ,
        variant,
        is_extension
    )?;

    if let Some(arguments) = arguments {
        arguments.codegen_arguments_lowlevel_pattern(sink, indent)?;
    } else {
        write!(sink, ", ..")?;
    }

    write!(sink, "}} if name == \"{}\" => {{\n", name)?;

    // Increment indent.
    {
        let indent = indent + TABSTOP;

        if let Some(arguments) = arguments {
            arguments.codegen_arguments_extraction(sink, indent)?;
        }

        write_indented!(sink, indent, "Ok(highlevel::{}::{} {{", typ, type_name)?;

        if let Some(arguments) = arguments {
            write_newline(sink)?;
            arguments.codegen_arguments_highlevel_parameters(sink, indent + TABSTOP)?;
            write_indent(sink, indent)?;
        }
        write!(sink, "}})\n")?;
    }
    write_indented!(sink, indent, "}},")?;

    Ok(())
}

fn codegen_extract_primitive<W: io::Write>(
    sink: &mut W,
    indent: u64,
    src_name: &str,
    dst_name: &str,
    extract_primitive_fn_name: &str,
    typ: &PrimitiveType,
) -> Result {
    write_indented!(
        sink,
        indent,
        "let {}_primitive = {}({}_raw, &arguments)?;\n",
        src_name,
        extract_primitive_fn_name,
        src_name
    )?;
    match typ {
        PrimitiveType::Integer => {
            write_indented!(
                sink,
                indent,
                "let {} = extract_int_from_primitive({}_primitive, &arguments)?;\n",
                dst_name,
                src_name
            )?;
        }
        PrimitiveType::BoolAsInt => {
            write_indented!(
                sink,
                indent,
                "let {}_int = extract_int_from_primitive({}_primitive, &arguments)?;\n",
                src_name,
                src_name
            )?;
            write_indented!(sink, indent, "let {} = {}_int != 0;\n", dst_name, src_name)?;
        }
        PrimitiveType::NamedType(type_name) => {
            write_indented!(
                sink,
                indent,
                "let {}_int = extract_int_from_primitive({}_primitive, &arguments)?;\n",
                src_name,
                src_name
            )?;
            write_indented!(
                sink,
                indent,
                "let {} = super::types::{}::from_i64({}_int).ok_or(DeserializeError::UnknownArguments(arguments.clone()))?;\n",
                dst_name,
                type_name,
                src_name
            )?;
        }
        PrimitiveType::String => {
            write_indented!(
                sink,
                indent,
                "let {} = extract_string_from_primitive({}_primitive, &arguments)?;\n",
                dst_name,
                src_name
            )?;
        }
    }
    Ok(())
}

fn codegen_extract_map<W: io::Write>(
    sink: &mut W,
    indent: u64,
    name: &str,
    key: &PrimitiveType,
    value: &PrimitiveType,
    initial_index: i64,
) -> Result {
    let element_name = format!("{}_element", name);
    let key_name = format!("{}_key", name);
    let value_name = format!("{}_value", name);

    write_indented!(sink, indent, "let mut {} = std::collections::HashMap::new();\n", name)?;
    write_indented!(
        sink,
        indent,
        "for {}_raw in arg_vec[{}..].into_iter() {{\n",
        element_name,
        initial_index
    )?;

    {
        let indent = indent + TABSTOP;

        codegen_extract_primitive(
            sink,
            indent,
            &element_name,
            &key_name,
            "extract_key_from_field",
            key,
        )?;
        codegen_extract_primitive(
            sink,
            indent,
            &element_name,
            &value_name,
            "extract_value_from_field",
            value,
        )?;

        write_indented!(sink, indent, "{}.insert({}, {});\n", name, key_name, value_name)?;
    }
    write_indented!(sink, indent, "}}   \n")?;

    Ok(())
}

fn codegen_extract_list<W: io::Write>(
    sink: &mut W,
    indent: u64,
    name: &str,
    typ: &PrimitiveType,
    initial_index: i64,
) -> Result {
    let element_name = format!("{}_element", name);

    write_indented!(sink, indent, "let mut {} = Vec::new();\n", name,)?;
    write_indented!(
        sink,
        indent,
        "for {}_raw in arg_vec[{}..].into_iter() {{\n",
        element_name,
        initial_index
    )?;

    {
        let indent = indent + TABSTOP;

        codegen_extract_primitive(
            sink,
            indent,
            &element_name,
            &element_name,
            "extract_primitive_from_field",
            typ,
        )?;

        write_indented!(sink, indent, "{}.push({});\n", name, element_name)?;
    }
    write_indented!(sink, indent, "}}\n")?;

    Ok(())
}

fn codegen_argument_vec_extraction<W: io::Write>(
    sink: &mut W,
    indent: u64,
    arg_vec: &[Argument],
) -> Result {
    let mut i = 0;
    for arg in arg_vec {
        match &arg.typ {
            Type::PrimitiveType(typ) => {
                write_indented!(sink, indent,
                    "let {}_raw = arg_vec.get({}).ok_or(DeserializeError::UnknownArguments(arguments.clone()))?;\n",
                    arg.name, i)?;
                codegen_extract_primitive(
                    sink,
                    indent,
                    &arg.name,
                    &arg.name,
                    "extract_primitive_from_field",
                    &typ,
                )?
            }
            Type::List(typ) => {
                codegen_extract_list(sink, indent, &arg.name, &typ, i)?;
                break;
            }
            Type::Map { key, value } => {
                codegen_extract_map(sink, indent, &arg.name, &key, &value, i)?;
                break;
            }
        }
        i += 1;
    }

    Ok(())
}

/// Specifies how to generate pieces of a match arm for both Arguments and ExecuteArguments.
trait CodegenArguments {
    fn codegen_arguments_lowlevel_pattern<W: io::Write>(&self, sink: &mut W, indent: u64)
        -> Result;
    fn codegen_arguments_extraction<W: io::Write>(&self, sink: &mut W, indent: u64) -> Result;
    fn codegen_arguments_highlevel_parameters<W: io::Write>(
        &self,
        sink: &mut W,
        indent: u64,
    ) -> Result;
}

impl CodegenArguments for Arguments {
    fn codegen_arguments_lowlevel_pattern<W: io::Write>(
        &self,
        sink: &mut W,
        _indent: u64,
    ) -> Result {
        if !self.is_empty() {
            write!(sink, ", arguments")?;
        } else {
            write!(sink, ", ..")?;
        }
        Ok(())
    }

    fn codegen_arguments_extraction<W: io::Write>(&self, sink: &mut W, indent: u64) -> Result {
        if !self.is_empty() {
            match self {
                Arguments::ParenthesisDelimitedArgumentLists(arg_vec_vec) => {
                    write_indented!(
                        sink,
                        indent,
                        "let arg_vec_vec = extract_vec_vec_from_args(&arguments)?;\n"
                    )?;
                    let mut i = 0;
                    for arg_vec in arg_vec_vec {
                        write_indented!(sink, indent,
                        "let arg_vec = arg_vec_vec.get({}).ok_or(DeserializeError::UnknownArguments(arguments.clone()))?;\n",
                         i
                    )?;
                        codegen_argument_vec_extraction(sink, indent, arg_vec)?;
                        i += 1;
                    }
                }
                Arguments::ArgumentList(arg_vec) => {
                    write_indented!(
                        sink,
                        indent,
                        "let arg_vec = extract_vec_from_args(&arguments)?;\n"
                    )?;
                    codegen_argument_vec_extraction(sink, indent, arg_vec)?;
                }
            }
        }
        Ok(())
    }

    fn codegen_arguments_highlevel_parameters<W: io::Write>(
        &self,
        sink: &mut W,
        indent: u64,
    ) -> Result {
        let arg_vec = match self {
            Arguments::ParenthesisDelimitedArgumentLists(arg_vec_vec) => arg_vec_vec.concat(),
            Arguments::ArgumentList(arg_vec) => arg_vec.clone(),
        };

        for arg in arg_vec {
            write_indented!(sink, indent, "{},\n", arg.name)?;
        }

        Ok(())
    }
}

impl CodegenArguments for ExecuteArguments {
    fn codegen_arguments_lowlevel_pattern<W: io::Write>(
        &self,
        sink: &mut W,
        _indent: u64,
    ) -> Result {
        let ExecuteArguments { nonstandard_delimiter, .. } = self;
        write!(sink,
            ", arguments: Some(lowlevel::ExecuteArguments {{ nonstandard_delimiter: {:?}, arguments }})",
            nonstandard_delimiter
        )?;

        Ok(())
    }

    fn codegen_arguments_extraction<W: io::Write>(&self, sink: &mut W, indent: u64) -> Result {
        let ExecuteArguments { arguments, .. } = self;
        arguments.codegen_arguments_extraction(sink, indent)
    }

    fn codegen_arguments_highlevel_parameters<W: io::Write>(
        &self,
        sink: &mut W,
        indent: u64,
    ) -> Result {
        let ExecuteArguments { arguments, .. } = self;
        arguments.codegen_arguments_highlevel_parameters(sink, indent)
    }
}
