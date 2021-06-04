// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The functions in this module convert the untyped pest Pair syntax nodes to typed AT command argument nodes.
///
/// The types in the arguments module represent the arguments of either AT commands and responses.  However, the
/// pest grammar macros generate different Rule types for the arguments of AT commands or responses.  Thus, the
/// argument parser must be abstracted over the Rule types.  In addition, the actual enum variants of the Rule
/// type differ between different generated macros, so we also abstract over the Rule enum values by storing them
/// in a struct.
///
/// These functions should not fail, but report failures from the next_* methods if the
/// expected element is not found.  This should only happen if the code here does not correctly
/// match the parse tree defined in argument_grammar.rs.
use {
    crate::{
        lowlevel::{Argument, Arguments, DelimitedArguments},
        parser::common::{
            next_match, next_match_one_of, next_match_option, next_match_rep, parse_string,
            ParseResult,
        },
    },
    pest::{iterators::Pair, RuleType},
};

pub struct ArgumentsParser<Rule: RuleType> {
    pub argument_list: Rule,
    pub argument: Rule,
    pub arguments: Rule,
    pub key_value_argument: Rule,
    pub optional_argument_delimiter: Rule,
    pub optional_argument_terminator: Rule,
    pub parenthesized_argument_lists: Rule,
    pub primitive_argument: Rule,
}

impl<Rule: RuleType> ArgumentsParser<Rule> {
    pub fn parse_delimited_arguments(
        &self,
        delimited_arguments: Pair<'_, Rule>,
    ) -> ParseResult<DelimitedArguments, Rule> {
        let mut delimited_arguments_elements = delimited_arguments.into_inner();

        let delimited_argument_delimiter_option =
            next_match_option(&mut delimited_arguments_elements, self.optional_argument_delimiter)?;
        let parsed_delimited_argument_delimiter_option = match delimited_argument_delimiter_option {
            Some(delimiter) => {
                let string = parse_string(delimiter)?;
                (!string.is_empty()).then(|| string)
            }
            None => None,
        };

        let arguments = next_match(&mut delimited_arguments_elements, self.arguments)?;
        let parsed_arguments = self.parse_arguments(arguments)?;

        let delimited_argument_terminator_option = next_match_option(
            &mut delimited_arguments_elements,
            self.optional_argument_terminator,
        )?;
        let parsed_delimited_argument_terminator_option = match delimited_argument_terminator_option
        {
            Some(terminator) => {
                let string = parse_string(terminator)?;
                (!string.is_empty()).then(|| string)
            }
            None => None,
        };

        Ok(DelimitedArguments {
            delimiter: parsed_delimited_argument_delimiter_option,
            arguments: parsed_arguments,
            terminator: parsed_delimited_argument_terminator_option,
        })
    }

    fn parse_arguments(&self, arguments: Pair<'_, Rule>) -> ParseResult<Arguments, Rule> {
        let mut arguments_elements = arguments.into_inner();
        let arguments_variant = next_match_one_of(
            &mut arguments_elements,
            vec![self.parenthesized_argument_lists, self.argument_list],
        )?;
        let arguments_variant_rule = arguments_variant.as_rule();
        let parsed_arguments = if arguments_variant_rule == self.parenthesized_argument_lists {
            self.parse_parenthesized_argument_lists(arguments_variant)?
        } else if arguments_variant_rule == self.argument_list {
            self.parse_argument_list(arguments_variant)?
        } else {
            // This is unreachable since next_match_one_of only returns success if one of the rules
            // passed into it matches; otherwise it returns Err and this method will return early
            // before reaching this point.
            unreachable!()
        };

        Ok(parsed_arguments)
    }

    fn parse_parenthesized_argument_lists(
        &self,
        parenthesized_argument_lists: Pair<'_, Rule>,
    ) -> ParseResult<Arguments, Rule> {
        let mut parenthesized_argument_lists_elements = parenthesized_argument_lists.into_inner();
        let argument_list_vec =
            next_match_rep(&mut parenthesized_argument_lists_elements, self.argument_list);

        let parsed_argument_list_vec = argument_list_vec
            .into_iter()
            .map(|argument_list_pair| self.parse_argument_list_to_vec(argument_list_pair))
            .collect::<ParseResult<Vec<Vec<Argument>>, Rule>>()?;

        Ok(Arguments::ParenthesisDelimitedArgumentLists(parsed_argument_list_vec))
    }

    fn parse_argument_list(&self, argument_list: Pair<'_, Rule>) -> ParseResult<Arguments, Rule> {
        let parsed_argument_list = self.parse_argument_list_to_vec(argument_list)?;
        Ok(Arguments::ArgumentList(parsed_argument_list))
    }

    fn parse_argument_list_to_vec(
        &self,
        argument_list: Pair<'_, Rule>,
    ) -> ParseResult<Vec<Argument>, Rule> {
        let mut argument_list_elements = argument_list.into_inner();
        let argument_vec = next_match_rep(&mut argument_list_elements, self.argument);

        let parsed_argument_vec = argument_vec
            .into_iter()
            .map(|argument_pair| self.parse_argument(argument_pair))
            .collect::<ParseResult<Vec<Argument>, Rule>>()?;

        // This is a hack.  There's no way for the parser to tell if "AT+CMD=" contains zero
        // arguments or one empty argument.  The parser eagerly assumes that it's one empty
        // argument.  However, if the AT command definition expects zero arguments this is an
        // error. On the other hand, missing optional argunments at the end of an AT command
        // are considered empty. Forcing this to be zero arguments here allows the raise step
        // to handle both the zero arguments case and the empty optional argument case.
        let parsed_argument_vec_maybe_empty =
            if parsed_argument_vec == vec![Argument::PrimitiveArgument(String::from(""))] {
                Vec::new()
            } else {
                parsed_argument_vec
            };

        Ok(parsed_argument_vec_maybe_empty)
    }

    fn parse_argument(&self, argument: Pair<'_, Rule>) -> ParseResult<Argument, Rule> {
        let mut argument_elements = argument.into_inner();

        let argument_variant = next_match_one_of(
            &mut argument_elements,
            vec![self.key_value_argument, self.primitive_argument],
        )?;

        let argument_variant_rule = argument_variant.as_rule();
        let parsed_argument = if argument_variant_rule == self.key_value_argument {
            self.parse_key_value_argument(argument_variant)?
        } else if argument_variant_rule == self.primitive_argument {
            Argument::PrimitiveArgument(parse_string(argument_variant)?)
        } else {
            // This is unreachable since next_match_one_of only returns success if one of the rules
            // passed into it matches; otherwise it returns Err and this method will return early
            // before reaching this point.
            unreachable!()
        };

        Ok(parsed_argument)
    }

    fn parse_key_value_argument(
        &self,
        key_value_argument: Pair<'_, Rule>,
    ) -> ParseResult<Argument, Rule> {
        let mut argument_elements = key_value_argument.into_inner();

        let key = next_match(&mut argument_elements, self.primitive_argument)?;
        let parsed_key = parse_string(key)?;

        let value = next_match(&mut argument_elements, self.primitive_argument)?;
        let parsed_value = parse_string(value)?;

        Ok(Argument::KeyValueArgument { key: parsed_key, value: parsed_value })
    }
}
