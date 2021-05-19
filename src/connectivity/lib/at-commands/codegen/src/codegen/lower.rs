// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module containing methods for generating code to "lower" from the high level, typed,
//! generated AT command and response types to the low level generic ASTs.

use {
    super::{
        common::{to_initial_capital, type_names::*, write_indent, write_newline, TABSTOP},
        error::Result,
    },
    crate::definition::{
        Argument, Arguments, Command, Definition, DelimitedArguments, PrimitiveType, Type,
    },
    std::io,
};

/// Entry point to generate `lower` methods at a given indentation.
pub fn codegen<W: io::Write>(sink: &mut W, indent: u64, definitions: &[Definition]) -> Result {
    codegen_commands(sink, indent, definitions)?;
    codegen_successes(sink, indent, definitions)
}

fn codegen_commands<W: io::Write>(sink: &mut W, indent: u64, definitions: &[Definition]) -> Result {
    write_indented!(
        sink,
        indent,
        "pub fn lower_command(highlevel: &highlevel::Command) -> lowlevel::Command {{\n"
    )?;

    // Increment indent
    {
        let indent = indent + TABSTOP;

        write_indented!(sink, indent, "match highlevel {{\n")?;

        // Increment indent
        {
            let indent = indent + TABSTOP;

            for definition in definitions {
                if let Definition::Command(command) = definition {
                    codegen_command(sink, indent, command)?;
                }
            }
        }

        write_indented!(sink, indent, "}}\n")?;
    }

    write_indented!(sink, indent, "}}\n\n")?;

    Ok(())
}

fn codegen_command<W: io::Write>(sink: &mut W, indent: u64, command: &Command) -> Result {
    match command {
        Command::Execute { name, type_name: _, is_extension, arguments } => {
            codegen_match_branch(
                sink,
                indent,
                name,
                LOWLEVEL_COMMAND_TYPE,
                LOWLEVEL_EXECUTE_COMMAND_VARIANT,
                HIGHLEVEL_COMMAND_TYPE,
                &command.type_name(),
                *is_extension,
                Some(arguments),
            )?;
        }
        Command::Read { name, type_name: _, is_extension } => {
            codegen_match_branch(
                sink,
                indent,
                name,
                LOWLEVEL_COMMAND_TYPE,
                LOWLEVEL_READ_COMMAND_VARIANT,
                HIGHLEVEL_COMMAND_TYPE,
                &command.type_name(),
                *is_extension,
                None::<&DelimitedArguments>,
            )?;
        }
        Command::Test { name, type_name: _, is_extension } => {
            codegen_match_branch(
                sink,
                indent,
                name,
                LOWLEVEL_COMMAND_TYPE,
                LOWLEVEL_TEST_COMMAND_VARIANT,
                HIGHLEVEL_COMMAND_TYPE,
                &command.type_name(),
                *is_extension,
                None::<&DelimitedArguments>,
            )?;
        }
    };
    write_newline(sink)?;

    Ok(())
}

fn codegen_successes<W: io::Write>(
    sink: &mut W,
    indent: u64,
    definitions: &[Definition],
) -> Result {
    write_indented!(
        sink,
        indent,
        "pub fn lower_success(highlevel: &highlevel::Success) -> lowlevel::Response {{\n"
    )?;

    // Increment indent
    {
        let indent = indent + TABSTOP;

        write_indented!(sink, indent, "match highlevel {{\n")?;

        // Increment indent
        {
            let indent = indent + TABSTOP;

            for definition in definitions {
                if let Definition::Response { name, type_name, is_extension, arguments } =
                    definition
                {
                    let type_name = type_name.clone().unwrap_or_else(|| to_initial_capital(name));
                    codegen_match_branch(
                        sink,
                        indent,
                        name,
                        LOWLEVEL_RESPONSE_TYPE,
                        LOWLEVEL_RESPONSE_VARIANT,
                        HIGHLEVEL_SUCCESS_TYPE,
                        &type_name,
                        *is_extension,
                        Some(arguments),
                    )?;
                    write_newline(sink)?;
                };
            }
        }
        write_indented!(sink, indent, "}}\n")?;
    }
    write_indented!(sink, indent, "}}\n\n")?;

    Ok(())
}

fn codegen_match_branch<W: io::Write>(
    sink: &mut W,
    indent: u64,
    name: &str,
    lowlevel_type: &str,
    lowlevel_variant: &str,
    highlevel_type: &str,
    highlevel_variant: &str,
    is_extension: bool,
    arguments: Option<&DelimitedArguments>,
) -> Result {
    write_indented!(sink, indent, "highlevel::{}::{} {{", highlevel_type, highlevel_variant,)?;

    if let Some(arguments) = arguments {
        codegen_delimited_arguments_highlevel_pattern(sink, indent + TABSTOP, arguments)?;
        write_indent(sink, indent)?;
    }
    write!(sink, "}} => {{\n")?;

    // Increment indent.
    {
        let indent = indent + TABSTOP;

        if let Some(arguments) = arguments {
            codegen_delimited_arguments_encoding(sink, indent, arguments)?;
        }

        write_indented!(
            sink,
            indent,
            "lowlevel::{}::{} {{ name: String::from(\"{}\"), is_extension: {}",
            lowlevel_type,
            lowlevel_variant,
            name,
            is_extension
        )?;

        if let Some(arguments) = arguments {
            codegen_delimited_arguments_lowlevel_parameters(sink, indent + TABSTOP, arguments)?;
        }
        write!(sink, " }}\n")?;
    }
    write_indented!(sink, indent, "}},")?;

    Ok(())
}

fn codegen_encode_primitive<W: io::Write>(
    sink: &mut W,
    indent: u64,
    src_name: &str,
    dst_name: &str,
    typ: &PrimitiveType,
) -> Result {
    match typ {
        PrimitiveType::Integer => {
            write_indented!(
                sink,
                indent,
                "let {} = lowlevel::PrimitiveArgument::Integer(*{});\n",
                dst_name,
                src_name
            )?;
        }
        PrimitiveType::BoolAsInt => {
            write_indented!(
                sink,
                indent,
                "let {}_int = if *{} {{ 1 }} else {{ 0 }};\n",
                dst_name,
                src_name
            )?;
            write_indented!(
                sink,
                indent,
                "let {} = lowlevel::PrimitiveArgument::Integer({}_int);\n",
                dst_name,
                dst_name
            )?;
        }
        PrimitiveType::NamedType(_type_name) => {
            write_indented!(sink, indent, "let {}_int = *{} as i64;\n", dst_name, src_name)?;
            write_indented!(
                sink,
                indent,
                "let {} = lowlevel::PrimitiveArgument::Integer({}_int);\n",
                dst_name,
                dst_name
            )?;
        }
        PrimitiveType::String => {
            write_indented!(
                sink,
                indent,
                "let {} = lowlevel::PrimitiveArgument::String({}.clone());\n",
                dst_name,
                src_name
            )?;
        }
    }
    Ok(())
}

fn codegen_encode_map<W: io::Write>(
    sink: &mut W,
    indent: u64,
    name: &str,
    key: &PrimitiveType,
    value: &PrimitiveType,
    arg_vec_name: &str,
) -> Result {
    let key_src_name = format!("{}_typed_key", name);
    let key_dst_name = format!("{}_primitive_key", name);
    let value_src_name = format!("{}_typed_value", name);
    let value_dst_name = format!("{}_primitive_value", name);
    let pair_name = format!("{}_untyped_pair", name);

    write_indented!(sink, indent, "for ({}, {}) in {} {{\n", key_src_name, value_src_name, name,)?;

    // Increment indent
    {
        let indent = indent + TABSTOP;

        codegen_encode_primitive(sink, indent, &key_src_name, &key_dst_name, key)?;
        codegen_encode_primitive(sink, indent, &value_src_name, &value_dst_name, value)?;

        write_indented!(
            sink,
            indent,
            "let {} = lowlevel::Argument::KeyValueArgument {{ key: {}, value: {} }};\n",
            pair_name,
            key_dst_name,
            value_dst_name
        )?;
        write_indented!(sink, indent, "{}.push({});\n", arg_vec_name, pair_name)?;
    }
    write_indented!(sink, indent, "}}   \n")?;

    Ok(())
}

fn codegen_encode_list<W: io::Write>(
    sink: &mut W,
    indent: u64,
    name: &str,
    typ: &PrimitiveType,
    arg_vec_name: &str,
) -> Result {
    let element_typed_name = format!("{}_typed_element", name);
    let element_primitive_name = format!("{}_primitive_element", name);
    let element_untyped_name = format!("{}_untyped_element", name);

    write_indented!(sink, indent, "for {} in {}.into_iter() {{\n", element_typed_name, name)?;

    // Increment indent
    {
        let indent = indent + TABSTOP;
        codegen_encode_primitive(
            sink,
            indent + TABSTOP,
            &element_typed_name,
            &element_primitive_name,
            typ,
        )?;
        write_indented!(
            sink,
            indent,
            "let {} = lowlevel::Argument::PrimitiveArgument({});\n",
            element_untyped_name,
            element_primitive_name
        )?;
        write_indented!(
            sink,
            indent + TABSTOP,
            "{}.push({});\n",
            arg_vec_name,
            element_untyped_name
        )?;
    }
    write_indented!(sink, indent, "}}   \n")?;

    Ok(())
}

fn codegen_argument_vec_encoding<W: io::Write>(
    sink: &mut W,
    indent: u64,
    arg_vec: &[Argument],
    arg_vec_name: &str,
) -> Result {
    for arg in arg_vec {
        match &arg.typ {
            Type::PrimitiveType(typ) => {
                let primitive_arg = format!("{}_primitive", arg.name);
                let untyped_arg = format!("{}_untyped", arg.name);
                codegen_encode_primitive(sink, indent, &arg.name, &primitive_arg, &typ)?;
                write_indented!(
                    sink,
                    indent,
                    "let {} = lowlevel::Argument::PrimitiveArgument({});\n",
                    untyped_arg,
                    primitive_arg
                )?;
                write_indented!(sink, indent, "{}.push({});\n", arg_vec_name, untyped_arg)?;
            }
            Type::Option(typ) => {
                let internal_arg = format!("{}_internal", arg.name);
                let primitive_arg = format!("{}_primitive", arg.name);
                let untyped_arg = format!("{}_untyped", arg.name);
                write_indented!(sink, indent, "if let Some({}) = {} {{\n", internal_arg, arg.name)?;
                codegen_encode_primitive(
                    sink,
                    indent + TABSTOP,
                    &internal_arg,
                    &primitive_arg,
                    &typ,
                )?;
                write_indented!(
                    sink,
                    indent + TABSTOP,
                    "let {} = lowlevel::Argument::PrimitiveArgument({});\n",
                    untyped_arg,
                    primitive_arg
                )?;
                write_indented!(
                    sink,
                    indent + TABSTOP,
                    "{}.push({});\n",
                    arg_vec_name,
                    untyped_arg
                )?;
                write_indented!(sink, indent, "}}\n")?;
            }
            Type::List(typ) => {
                codegen_encode_list(sink, indent, &arg.name, typ, arg_vec_name)?;
                break;
            }
            Type::Map { key, value } => {
                codegen_encode_map(sink, indent, &arg.name, &key, &value, arg_vec_name)?;
                break;
            }
        }
    }

    Ok(())
}

fn codegen_arguments_highlevel_pattern<W: io::Write>(
    sink: &mut W,
    indent: u64,
    arguments: &Arguments,
) -> Result {
    if !arguments.is_empty() {
        let arg_vec = arguments.flatten();

        write_newline(sink)?;

        for arg in arg_vec {
            write_indented!(sink, indent, "{},\n", arg.name)?;
        }
    }
    Ok(())
}

fn codegen_arguments_encoding<W: io::Write>(
    sink: &mut W,
    indent: u64,
    arguments: &Arguments,
) -> Result {
    if !arguments.is_empty() {
        match arguments {
            Arguments::ParenthesisDelimitedArgumentLists(arg_vec_vec) => {
                write_indented!(
                    sink,
                    indent,
                    "let mut raw_arguments_outer = Vec::<Vec<lowlevel::Argument>>::new();\n"
                )?;
                for arg_vec in arg_vec_vec {
                    write_indented!(
                        sink,
                        indent,
                        "let mut raw_arguments_inner = Vec::<lowlevel::Argument>::new();\n"
                    )?;
                    codegen_argument_vec_encoding(sink, indent, arg_vec, "raw_arguments_inner")?;
                    write_indented!(
                        sink,
                        indent,
                        "raw_arguments_outer.push(raw_arguments_inner);\n"
                    )?;
                }
                write_indented!(
                        sink,
                        indent,
                        "let arguments = lowlevel::Arguments::ParenthesisDelimitedArgumentLists(raw_arguments_outer);\n"
                    )?;
            }
            Arguments::ArgumentList(arg_vec) => {
                write_indented!(
                    sink,
                    indent,
                    "let mut raw_arguments = Vec::<lowlevel::Argument>::new();\n"
                )?;
                codegen_argument_vec_encoding(sink, indent, arg_vec, "raw_arguments")?;
                write_indented!(
                    sink,
                    indent,
                    "let arguments = lowlevel::Arguments::ArgumentList(raw_arguments);\n"
                )?;
            }
        }
    } else {
        write_indented!(
            sink,
            indent,
            "let arguments = lowlevel::Arguments::ArgumentList(Vec::new());\n"
        )?;
    }
    Ok(())
}

fn codegen_delimited_arguments_highlevel_pattern<W: io::Write>(
    sink: &mut W,
    indent: u64,
    delimited_arguments: &DelimitedArguments,
) -> Result {
    let DelimitedArguments { arguments, .. } = delimited_arguments;
    codegen_arguments_highlevel_pattern(sink, indent, arguments)
}

fn codegen_delimited_arguments_encoding<W: io::Write>(
    sink: &mut W,
    indent: u64,
    delimited_arguments: &DelimitedArguments,
) -> Result {
    let DelimitedArguments { arguments, .. } = delimited_arguments;
    codegen_arguments_encoding(sink, indent, arguments)
}

fn codegen_delimited_arguments_lowlevel_parameters<W: io::Write>(
    sink: &mut W,
    _indent: u64,
    arguments: &DelimitedArguments,
) -> Result {
    let DelimitedArguments { delimiter, terminator, .. } = arguments;
    let delimiter_string = match delimiter {
        Some(del) => format!("Some(String::from(\"{}\"))", del),
        None => String::from("None"),
    };
    let terminator_string = match terminator {
        Some(term) => format!("Some(String::from(\"{}\"))", term),
        None => String::from("None"),
    };
    write!(
        sink,
        ", arguments: lowlevel::DelimitedArguments {{ delimiter: {}, arguments, terminator: {} }}",
        delimiter_string, terminator_string
    )?;

    Ok(())
}
