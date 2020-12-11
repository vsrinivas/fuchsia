// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The functions in this module convert the untyped pest Pair syntax nodes to typed AT response AST nodes.
///
/// These functions should not fail, but report failures from the next_* methods if the
/// expected element is not found.  This should only happen if the code here does not correctly
/// match the parse tree defined in response_grammar.rs.
use {
    crate::lowlevel::response::{HardcodedError, Response},
    crate::parser::{
        arguments_parser::ArgumentsParser,
        common::{
            next_match, next_match_one_of, parse_integer, parse_name, parse_string, ParseError,
            ParseResult,
        },
        response_grammar::{Grammar, Rule},
    },
    pest::{iterators::Pair, Parser},
};

static ARGUMENTS_PARSER: ArgumentsParser<Rule> = ArgumentsParser {
    argument: Rule::argument,
    argument_list: Rule::argument_list,
    integer: Rule::integer,
    key_value_argument: Rule::key_value_argument,
    parenthesized_argument_lists: Rule::parenthesized_argument_lists,
    primitive_argument: Rule::primitive_argument,
    string: Rule::string,
};

pub fn parse(string: &String) -> ParseResult<Response, Rule> {
    let mut parsed = Grammar::parse(Rule::input, string).map_err(|pest_error| {
        ParseError::PestParseFailure { string: string.clone(), pest_error }
    })?;

    let input = next_match(&mut parsed, Rule::input)?;
    let mut input_elements = input.into_inner();
    let response = next_match(&mut input_elements, Rule::response)?;

    parse_response(response)
}

fn parse_response(response: Pair<'_, Rule>) -> ParseResult<Response, Rule> {
    let mut response_elements = response.into_inner();
    let response_variant = next_match_one_of(
        &mut response_elements,
        vec![Rule::ok, Rule::error, Rule::hardcoded_error, Rule::cme_error, Rule::success],
    )?;

    let parsed_response = match response_variant.as_rule() {
        Rule::ok => Response::Ok,
        Rule::error => Response::Error,
        Rule::hardcoded_error => parse_hardcoded_error(response_variant)?,
        Rule::cme_error => parse_cme_error(response_variant)?,
        Rule::success => parse_success(response_variant)?,
        // This is unreachable since next_match_one_of only returns success if one of the rules
        // passed into it matches; otherwise it returns Err and this method will return early
        // before reaching this point.
        _ => unreachable!(),
    };

    Ok(parsed_response)
}

fn parse_hardcoded_error(hardcoded_error: Pair<'_, Rule>) -> ParseResult<Response, Rule> {
    let error_string = parse_string(hardcoded_error)?;

    let parsed_error = match error_string.as_str() {
        "NO CARRIER" => HardcodedError::NoCarrier,
        "BUSY" => HardcodedError::Busy,
        "NO ANSWER" => HardcodedError::NoAnswer,
        "DELAYED" => HardcodedError::Delayed,
        "BLACKLIST" => HardcodedError::Blacklist,
        // This is unreachable since these are the only strings that match the harcoded_error rule.
        // For parse_response to call this method, hardcoded_error must have matched this span of the
        // parsed response, so only the above strings are possible matches.
        _ => unreachable!(),
    };

    Ok(Response::HardcodedError(parsed_error))
}

fn parse_cme_error(cme_error: Pair<'_, Rule>) -> ParseResult<Response, Rule> {
    let mut cme_error_elements = cme_error.into_inner();

    let error_code = next_match(&mut cme_error_elements, Rule::integer)?;
    let parsed_error_code = parse_integer(error_code)?;

    Ok(Response::CmeError(parsed_error_code))
}

fn parse_success(success: Pair<'_, Rule>) -> ParseResult<Response, Rule> {
    let mut success_elements = success.into_inner();

    let optional_extension = next_match(&mut success_elements, Rule::optional_extension)?;
    let parsed_optional_extension = parse_optional_extension(optional_extension)?;

    let name = next_match(&mut success_elements, Rule::command_name)?;
    let parsed_name = parse_name(name)?;

    let arguments = next_match(&mut success_elements, Rule::arguments)?;
    let parsed_arguments = ARGUMENTS_PARSER.parse_arguments(arguments)?;

    Ok(Response::Success {
        name: parsed_name,
        is_extension: parsed_optional_extension,
        arguments: parsed_arguments,
    })
}

fn parse_optional_extension(optional_extension: Pair<'_, Rule>) -> ParseResult<bool, Rule> {
    let extension_str = optional_extension.as_span().as_str();

    match extension_str {
        "" => Ok(false),
        "+" => Ok(true),
        c => Err(ParseError::UnknownExtensionCharacter(c.to_string())),
    }
}
