// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains code to generate types representing AT commands and responses.

use {
    super::{
        common::{
            codegen_block, to_initial_capital,
            type_names::{HIGHLEVEL_COMMAND_TYPE, HIGHLEVEL_SUCCESS_TYPE},
            write_indent, write_newline,
        },
        error::{Error, Result},
    },
    crate::definition::{
        Argument, Arguments, Command, Definition, DelimitedArguments, PossiblyOptionType,
        PrimitiveType, Type, Variant,
    },
    std::collections::HashSet,
    std::io,
};

pub fn codegen<W: io::Write>(sink: &mut W, indent: u64, definitions: &[Definition]) -> Result {
    // Commands and responses are variants in enums, so they all need to be defined together.  So we split
    // the definitions up by type, define enums first, and define commands and responses later.

    codegen_enums(sink, indent, definitions)?;
    codegen_commands(sink, indent, definitions)?;
    codegen_responses(sink, indent, definitions)
}

pub fn codegen_enums<W: io::Write>(
    sink: &mut W,
    indent: u64,
    definitions: &[Definition],
) -> Result {
    for definition in definitions {
        if let Definition::Enum { name, variants } = definition {
            codegen_enum(sink, indent, name, variants)?;
        }
    }
    Ok(())
}

pub fn codegen_commands<W: io::Write>(
    sink: &mut W,
    indent: u64,
    definitions: &[Definition],
) -> Result {
    write_indented!(sink, indent, "#[derive(Clone, Debug, PartialEq, Eq)]\n")?;
    codegen_block(
        sink,
        indent,
        Some("pub enum"),
        HIGHLEVEL_COMMAND_TYPE,
        |sink, indent| {
            for definition in definitions {
                if let Definition::Command(command) = definition {
                    codegen_command(sink, indent, command)?;
                }
            }
            Ok(())
        },
        None,
    )?;
    write_newline(sink)
}

pub fn codegen_responses<W: io::Write>(
    sink: &mut W,
    indent: u64,
    definitions: &[Definition],
) -> Result {
    write_indented!(sink, indent, "#[derive(Clone, Debug, PartialEq, Eq)]\n")?;
    codegen_block(
        sink,
        indent,
        Some("pub enum"),
        HIGHLEVEL_SUCCESS_TYPE,
        |sink, indent| {
            for definition in definitions {
                if let Definition::Response { name, type_name, arguments, .. } = definition {
                    let type_name = type_name.clone().unwrap_or_else(|| to_initial_capital(name));
                    codegen_response(sink, indent, &type_name, arguments)?;
                }
            }
            Ok(())
        },
        None,
    )?;
    write_newline(sink)
}

fn codegen_response<W: io::Write>(
    sink: &mut W,
    indent: u64,
    type_name: &str,
    arguments: &DelimitedArguments,
) -> Result {
    codegen_block(
        sink,
        indent,
        None,
        type_name,
        |sink, indent| codegen_delimited_arguments(sink, indent, arguments),
        Some(","),
    )
}

fn codegen_enum<W: io::Write>(
    sink: &mut W,
    indent: u64,
    name: &str,
    variants: &[Variant],
) -> Result {
    write_indented!(sink, indent, "#[derive(Copy, Clone, Debug, FromPrimitive, PartialEq, Eq)]\n")?;
    codegen_block(
        sink,
        indent,
        Some("pub enum"),
        name,
        |sink, indent| {
            for variant in variants {
                codegen_variant(sink, indent, variant)?;
            }
            Ok(())
        },
        None,
    )?;

    write_newline(sink)
}

fn codegen_variant<W: io::Write>(sink: &mut W, indent: u64, variant: &Variant) -> Result {
    let Variant { name, value } = variant;
    write_indented!(sink, indent, "{} = {},\n", name, value)?;

    Ok(())
}

fn codegen_command<W: io::Write>(sink: &mut W, indent: u64, command: &Command) -> Result {
    match command {
        Command::Execute { arguments, .. } => {
            codegen_execute_command(sink, indent, &command.type_name(), arguments)
        }
        Command::Read { .. } => codegen_read_command(sink, indent, &command.type_name()),
        Command::Test { .. } => codegen_test_command(sink, indent, &command.type_name()),
    }
}

fn codegen_execute_command<W: io::Write>(
    sink: &mut W,
    indent: u64,
    type_name: &str,
    arguments: &DelimitedArguments,
) -> Result {
    codegen_block(
        sink,
        indent,
        None,
        type_name,
        |sink, indent| codegen_delimited_arguments(sink, indent, arguments),
        Some(","),
    )
}

fn codegen_read_command<W: io::Write>(sink: &mut W, indent: u64, type_name: &str) -> Result {
    codegen_block(sink, indent, None, type_name, |_sink, _indent| Ok(()), Some(","))
}

fn codegen_test_command<W: io::Write>(sink: &mut W, indent: u64, type_name: &str) -> Result {
    codegen_block(sink, indent, None, type_name, |_sink, _indent| Ok(()), Some(","))
}

fn codegen_delimited_arguments<W: io::Write>(
    sink: &mut W,
    indent: u64,
    execute_arguments: &DelimitedArguments,
) -> Result {
    let DelimitedArguments { delimiter: _, arguments, terminator: _ } = execute_arguments;
    codegen_arguments(sink, indent, arguments)
}

fn validate_name_uniqueness(
    arg_names: &mut HashSet<String>,
    arg: &Argument,
    args_for_error_reporting: &Arguments,
) -> Result {
    if arg_names.contains(&arg.name) {
        return Err(Error::DuplicateArgumentName(args_for_error_reporting.clone()));
    }
    let _ = arg_names.insert(arg.name.clone());
    Ok(())
}

fn validate_arg_vec(
    arg_names: &mut HashSet<String>,
    arg_vec: &Vec<Argument>,
    args_for_error_reporting: &Arguments,
) -> Result {
    if arg_vec.len() > 0 {
        for arg in &arg_vec[..arg_vec.len() - 1] {
            validate_name_uniqueness(arg_names, arg, args_for_error_reporting)?;
            match arg.typ {
                Type::PossiblyOptionType(_) => {}
                Type::ListType(_) => {
                    return Err(Error::ListMustBeLast(args_for_error_reporting.clone()))
                }
                Type::MapType { .. } => {
                    return Err(Error::MapMustBeLast(args_for_error_reporting.clone()))
                }
            }
        }
        validate_name_uniqueness(arg_names, &arg_vec[arg_vec.len() - 1], args_for_error_reporting)
    } else {
        Ok(())
    }
}

fn validate_arg_list(arg_vec: &Vec<Argument>, args_for_error_reporting: &Arguments) -> Result {
    let mut arg_names = HashSet::<String>::new();
    validate_arg_vec(&mut arg_names, arg_vec, args_for_error_reporting)
}

fn validate_paren_arg_lists(
    arg_vec_vec: &Vec<Vec<Argument>>,
    args_for_error_reporting: &Arguments,
) -> Result {
    let mut arg_names = HashSet::<String>::new();
    for arg_vec in arg_vec_vec {
        validate_arg_vec(&mut arg_names, arg_vec, args_for_error_reporting)?;
    }

    Ok(())
}

fn codegen_arguments<W: io::Write>(sink: &mut W, indent: u64, arguments: &Arguments) -> Result {
    match arguments {
        Arguments::ParenthesisDelimitedArgumentLists(args) => {
            validate_paren_arg_lists(args, arguments)?;
            for arg in args.concat() {
                codegen_argument(sink, indent, &arg)?;
            }
        }
        Arguments::ArgumentList(args) => {
            validate_arg_list(args, arguments)?;
            for arg in args {
                codegen_argument(sink, indent, arg)?;
            }
        }
    };
    Ok(())
}

fn codegen_argument<W: io::Write>(sink: &mut W, indent: u64, argument: &Argument) -> Result {
    let Argument { name, typ } = argument;
    write_indented!(sink, indent, "{}: ", name)?;
    codegen_type(sink, indent, typ)?;
    write_no_indent!(sink, ",\n")?;

    Ok(())
}

fn codegen_type<W: io::Write>(sink: &mut W, indent: u64, typ: &Type) -> Result {
    match typ {
        Type::ListType(typ) => codegen_list_type(sink, indent, typ),
        Type::MapType { key, value } => codegen_map_type(sink, indent, key, value),
        Type::PossiblyOptionType(typ) => codegen_possibly_option_type(sink, indent, typ),
    }
}

fn codegen_possibly_option_type<W: io::Write>(
    sink: &mut W,
    indent: u64,
    typ: &PossiblyOptionType,
) -> Result {
    match typ {
        PossiblyOptionType::PrimitiveType(typ) => codegen_primitive_type(sink, indent, typ)?,
        PossiblyOptionType::OptionType(typ) => {
            write_no_indent!(sink, "Option<")?;
            codegen_primitive_type(sink, indent, typ)?;
            write_no_indent!(sink, ">")?;
        }
    }

    Ok(())
}

fn codegen_list_type<W: io::Write>(sink: &mut W, indent: u64, typ: &PossiblyOptionType) -> Result {
    write_no_indent!(sink, "Vec<")?;
    codegen_possibly_option_type(sink, indent, typ)?;
    write_no_indent!(sink, ">")?;

    Ok(())
}

fn codegen_map_type<W: io::Write>(
    sink: &mut W,
    indent: u64,
    key_type: &PrimitiveType,
    value_type: &PrimitiveType,
) -> Result {
    write_no_indent!(sink, "std::collections::HashMap<")?;
    codegen_primitive_type(sink, indent, key_type)?;
    write_no_indent!(sink, ",")?;
    codegen_primitive_type(sink, indent, value_type)?;
    write_no_indent!(sink, ">")?;

    Ok(())
}

fn codegen_primitive_type<W: io::Write>(
    sink: &mut W,
    _indent: u64,
    primitive_type: &PrimitiveType,
) -> Result {
    match primitive_type {
        PrimitiveType::String => write_no_indent!(sink, "String")?,
        PrimitiveType::Integer => write_no_indent!(sink, "i64")?,
        PrimitiveType::BoolAsInt => write_no_indent!(sink, "bool")?,
        PrimitiveType::NamedType(name) => write_no_indent!(sink, "{}", name)?,
    };
    Ok(())
}
