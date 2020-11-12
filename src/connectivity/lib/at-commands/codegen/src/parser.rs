// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The functions in this module convert the untyped pest Pair syntax nodes to typed Definitions.
/// The next_* functions take a Pairs, an iterator over all the child syntax
/// nodes of a given syntactic element, and check to make sure the next element is of the
/// correct type or Rule.  The rest of the functions recursively convert untyped pest trees to
/// the appropriate typed definition types.
///
/// Most of these functions should not fail, but report failures from the next_* methods if the
/// expected element is not found.  This should only happen if the code here does not correctly
/// match the parse tree defined in grammar.rs.
use {
    crate::definition::{
        Argument, Arguments, Command, Definition, ExecuteArguments, PrimitiveType, Type, Variant,
    },
    crate::grammar::{Grammar, Rule},
    anyhow::{bail, ensure, Context, Result},
    pest::Parser,
};

/// The result of parsing text with single pest match rule.
type Pair<'a> = pest::iterators::Pair<'a, Rule>;

/// An interator over subelements of a a Pair.
type Pairs<'a> = pest::iterators::Pairs<'a, Rule>;

/// Get the next Pair syntax node out of an iterator if it is matched by one of the expected Rules.
/// Otherwise, fail.
fn next_match_one_of<'a>(pairs: &mut Pairs<'a>, expected_rules: Vec<Rule>) -> Result<Pair<'a>> {
    let pair_result = pairs.next();
    let pair = pair_result.with_context(|| {
        format!(
            "Unable to read parsed AT definition: expected one of {:?}, got nothing.",
            expected_rules
        )
    })?;

    let pair_rule = pair.as_rule();
    ensure!(
        expected_rules.contains(&pair_rule),
        "Unable to read parsed AT definition: expected one of {:?}, got {:?}.",
        expected_rules,
        pair_rule
    );

    Ok(pair)
}

/// Get the next Pair syntax node out of an iterator if it is matched by the expected Rule.
/// Otherwise, fail.
fn next_match<'a>(pairs: &mut Pairs<'a>, expected_rule: Rule) -> Result<Pair<'a>> {
    next_match_one_of(pairs, vec![expected_rule])
}

/// Get the next Pair syntax node out of an interator if it exists.  If it doesn't exist, return
/// None.  If it does exist and is not matched by the specified Rule, fail.
fn next_match_option<'a>(pairs: &mut Pairs<'a>, expected_rule: Rule) -> Result<Option<Pair<'a>>> {
    if !pairs.peek().is_some() {
        Ok(None)
    } else {
        next_match(pairs, expected_rule).map(|pair| Some(pair))
    }
}

/// Continue getting the next Pair syntax node of the iterator as long as theyare  matched by the
/// expected Rule.  Return a vector of matching Pairs.
fn next_match_rep<'a>(pairs: &mut Pairs<'a>, expected_rule: Rule) -> Vec<Pair<'a>> {
    let is_correct_rule = |pair_option: Option<Pair<'a>>| match pair_option {
        Some(pair) => pair.as_rule() == expected_rule,
        None => false,
    };

    let mut vector = Vec::new();
    while is_correct_rule(pairs.peek()) {
        let pair = pairs.next().unwrap();
        vector.push(pair);
    }

    vector
}

pub fn parse(string: &String) -> Result<Vec<Definition>> {
    let mut parsed =
        Grammar::parse(Rule::file, string).with_context(|| "Unable to parse AT definition.")?;

    let file = next_match(&mut parsed, Rule::file)?;

    parse_file(file)
}

fn parse_file(file_pair: Pair<'_>) -> Result<Vec<Definition>> {
    let mut file_elements = file_pair.into_inner();
    let definition_pair_vec = next_match_rep(&mut file_elements, Rule::definition);

    let parsed_definition_vec = definition_pair_vec
        .into_iter()
        .map(|definition_pair| parse_definition(definition_pair))
        .collect::<Result<Vec<Definition>>>()?;

    Ok(parsed_definition_vec)
}

fn parse_definition(definition: Pair<'_>) -> Result<Definition> {
    let mut definition_elements = definition.into_inner();
    let definition_variant = next_match_one_of(
        &mut definition_elements,
        vec![Rule::command, Rule::response, Rule::enumeration],
    )?;

    let parsed_definition = match definition_variant.as_rule() {
        Rule::command => parse_command(definition_variant),
        Rule::response => parse_response(definition_variant),
        Rule::enumeration => parse_enum(definition_variant),
        _ => unreachable!(),
    }?;

    Ok(parsed_definition)
}

fn parse_command(command: Pair<'_>) -> Result<Definition> {
    let mut command_elments = command.into_inner();
    let command_variant =
        next_match_one_of(&mut command_elments, vec![Rule::execute, Rule::read, Rule::test])?;

    let parsed_command = match command_variant.as_rule() {
        Rule::execute => parse_execute(command_variant),
        Rule::read => parse_read(command_variant),
        Rule::test => parse_test(command_variant),
        _ => unreachable!(),
    }?;

    Ok(Definition::Command(parsed_command))
}

fn parse_execute(execute: Pair<'_>) -> Result<Command> {
    let mut execute_elements = execute.into_inner();

    let optional_extension = next_match(&mut execute_elements, Rule::optional_extension)?;
    let parsed_optional_extension = parse_optional_extension(optional_extension)?;

    let name = next_match(&mut execute_elements, Rule::command_name)?;
    let parsed_name = parse_name(name)?;

    let execute_arguments_option =
        next_match_option(&mut execute_elements, Rule::execute_arguments)?;
    let parsed_execute_arguments_option = match execute_arguments_option {
        None => None,
        Some(execute_arguments) => parse_execute_arguments(execute_arguments)?,
    };

    Ok(Command::Execute {
        name: parsed_name,
        is_extension: parsed_optional_extension,
        arguments: parsed_execute_arguments_option,
    })
}

fn parse_execute_arguments(execute_arguments: Pair<'_>) -> Result<Option<ExecuteArguments>> {
    let mut execute_arguments_elements = execute_arguments.into_inner();

    let execute_argument_delimiter =
        next_match(&mut execute_arguments_elements, Rule::execute_argument_delimiter)?;
    let parsed_execute_argument_delimiter =
        parse_execute_argument_delimiter(execute_argument_delimiter)?;

    let arguments = next_match(&mut execute_arguments_elements, Rule::arguments)?;
    let parsed_arguments = parse_arguments(arguments)?;

    Ok(Some(ExecuteArguments {
        nonstandard_delimiter: parsed_execute_argument_delimiter,
        arguments: parsed_arguments,
    }))
}

fn parse_execute_argument_delimiter(
    execute_argument_delimiter: Pair<'_>,
) -> Result<Option<String>> {
    let execute_argument_delimiter_str = execute_argument_delimiter.as_span().as_str();
    if execute_argument_delimiter_str == "=" {
        Ok(None) // Standard delimiter
    } else {
        Ok(Some(execute_argument_delimiter_str.to_string())) // Nonstanded delimiter
    }
}

fn parse_read(read: Pair<'_>) -> Result<Command> {
    let mut read_elements = read.into_inner();

    let optional_extension = next_match(&mut read_elements, Rule::optional_extension)?;
    let parsed_optional_extension = parse_optional_extension(optional_extension)?;

    let name = next_match(&mut read_elements, Rule::command_name)?;
    let parsed_name = parse_name(name)?;

    Ok(Command::Read { name: parsed_name, is_extension: parsed_optional_extension })
}

fn parse_test(test: Pair<'_>) -> Result<Command> {
    let mut test_elements = test.into_inner();

    let optional_extension = next_match(&mut test_elements, Rule::optional_extension)?;
    let parsed_optional_extension = parse_optional_extension(optional_extension)?;

    let name = next_match(&mut test_elements, Rule::command_name)?;
    let parsed_name = parse_name(name)?;

    Ok(Command::Test { name: parsed_name, is_extension: parsed_optional_extension })
}

fn parse_response(response: Pair<'_>) -> Result<Definition> {
    let mut response_elements = response.into_inner();

    let optional_extension = next_match(&mut response_elements, Rule::optional_extension)?;
    let parsed_optional_extension = parse_optional_extension(optional_extension)?;

    let name = next_match(&mut response_elements, Rule::command_name)?;
    let parsed_name = parse_name(name)?;

    let arguments = next_match(&mut response_elements, Rule::arguments)?;
    let parsed_arguments = parse_arguments(arguments)?;

    Ok(Definition::Response {
        name: parsed_name,
        is_extension: parsed_optional_extension,
        arguments: parsed_arguments,
    })
}

fn parse_optional_extension(optional_extension: Pair<'_>) -> Result<bool> {
    let extension_str = optional_extension.as_span().as_str();

    match extension_str {
        "" => Ok(false),
        "+" => Ok(true),
        _ => bail!("Unexpected character after \"AT\": {:}", extension_str),
    }
}

fn parse_arguments(arguments: Pair<'_>) -> Result<Arguments> {
    let mut arguments_elements = arguments.into_inner();
    let arguments_variant = next_match_one_of(
        &mut arguments_elements,
        vec![Rule::parenthesized_argument_lists, Rule::argument_list],
    )?;

    let parsed_arguments = match arguments_variant.as_rule() {
        Rule::parenthesized_argument_lists => parse_parenthesized_argument_lists(arguments_variant),
        Rule::argument_list => parse_argument_list(arguments_variant),
        _ => unreachable!(),
    }?;

    Ok(parsed_arguments)
}

fn parse_parenthesized_argument_lists(parenthesized_argument_lists: Pair<'_>) -> Result<Arguments> {
    let mut parenthesized_argument_lists_elements = parenthesized_argument_lists.into_inner();
    let argument_list_vec =
        next_match_rep(&mut parenthesized_argument_lists_elements, Rule::argument_list);

    let parsed_argument_list_vec = argument_list_vec
        .into_iter()
        .map(|argument_list_pair| parse_argument_list_to_vec(argument_list_pair))
        .collect::<Result<Vec<Vec<Argument>>>>()?;

    Ok(Arguments::ParenthesisDelimitedArgumentLists(parsed_argument_list_vec))
}

fn parse_argument_list(argument_list: Pair<'_>) -> Result<Arguments> {
    let parsed_argument_list = parse_argument_list_to_vec(argument_list)?;
    Ok(Arguments::ArgumentList(parsed_argument_list))
}

fn parse_argument_list_to_vec(argument_list: Pair<'_>) -> Result<Vec<Argument>> {
    let mut argument_list_elements = argument_list.into_inner();
    let argument_vec = next_match_rep(&mut argument_list_elements, Rule::argument);

    let parsed_argument_vec = argument_vec
        .into_iter()
        .map(|argument_pair| parse_argument(argument_pair))
        .collect::<Result<Vec<Argument>>>()?;

    Ok(parsed_argument_vec)
}

fn parse_argument(argument: Pair<'_>) -> Result<Argument> {
    let mut argument_elements = argument.into_inner();

    let identifier = next_match(&mut argument_elements, Rule::identifier)?;
    let parsed_identifier = parse_identifier(identifier)?;

    let typ = next_match(&mut argument_elements, Rule::typ)?;
    let parsed_type = parse_type(typ)?;

    Ok(Argument { name: parsed_identifier, typ: parsed_type })
}

fn parse_type(typ: Pair<'_>) -> Result<Type> {
    let mut type_elements = typ.into_inner();
    let type_variant = next_match_one_of(
        &mut type_elements,
        vec![Rule::list_type, Rule::map_type, Rule::identifier],
    )?;

    let parsed_type = match type_variant.as_rule() {
        Rule::list_type => parse_list_type(type_variant)?,
        Rule::map_type => parse_map_type(type_variant)?,
        Rule::identifier => Type::PrimitiveType(parse_primitive_type(type_variant)?),
        _ => unreachable!(),
    };

    Ok(parsed_type)
}

fn parse_list_type(list_type: Pair<'_>) -> Result<Type> {
    let mut list_type_elements = list_type.into_inner();

    let element_type = next_match(&mut list_type_elements, Rule::identifier)?;
    let parsed_element_type = parse_primitive_type(element_type)?;

    Ok(Type::List(parsed_element_type))
}

fn parse_map_type(map_type: Pair<'_>) -> Result<Type> {
    let mut map_type_elements = map_type.into_inner();

    let key_type = next_match(&mut map_type_elements, Rule::identifier)?;
    let parsed_key_type = parse_primitive_type(key_type)?;

    let value_type = next_match(&mut map_type_elements, Rule::identifier)?;
    let parsed_value_type = parse_primitive_type(value_type)?;

    Ok(Type::Map { key: parsed_key_type, value: parsed_value_type })
}

fn parse_primitive_type(primitive_type: Pair<'_>) -> Result<PrimitiveType> {
    let string = parse_identifier(primitive_type)?;

    let parsed_primitive_type = match string.as_str() {
        "Integer" => PrimitiveType::Integer,
        "String" => PrimitiveType::String,
        "BoolAsInt" => PrimitiveType::BoolAsInt,
        name => PrimitiveType::NamedType(name.to_string()),
    };

    Ok(parsed_primitive_type)
}

fn parse_enum(enumeration: Pair<'_>) -> Result<Definition> {
    let mut enum_elements = enumeration.into_inner();

    let name = next_match(&mut enum_elements, Rule::identifier)?;
    let parsed_name = parse_identifier(name)?;

    let variants = next_match(&mut enum_elements, Rule::variants)?;
    let parsed_variants = parse_variants(variants)?;

    Ok(Definition::Enum { name: parsed_name, variants: parsed_variants })
}

fn parse_variants(variants: Pair<'_>) -> Result<Vec<Variant>> {
    let mut variants_elements = variants.into_inner();
    let variant_vec = next_match_rep(&mut variants_elements, Rule::variant);

    let parsed_variant_vec = variant_vec
        .into_iter()
        .map(|variant_pair| parse_variant(variant_pair))
        .collect::<Result<Vec<Variant>>>()?;

    Ok(parsed_variant_vec)
}

fn parse_variant(variant: Pair<'_>) -> Result<Variant> {
    let mut variant_elements = variant.into_inner();

    let name = next_match(&mut variant_elements, Rule::identifier)?;
    let parsed_name = parse_identifier(name)?;

    let value = next_match(&mut variant_elements, Rule::integer)?;
    let parsed_value = parse_integer(value)?;

    Ok(Variant { name: parsed_name, value: parsed_value })
}

fn parse_name(name: Pair<'_>) -> Result<String> {
    Ok(name.as_span().as_str().to_string())
}

fn parse_identifier(identifier: Pair<'_>) -> Result<String> {
    Ok(identifier.as_span().as_str().to_string())
}

fn parse_integer(integer: Pair<'_>) -> Result<i64> {
    Ok(integer.as_span().as_str().parse()?) // Convert to anyhow
}
