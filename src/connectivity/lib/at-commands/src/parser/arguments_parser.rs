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
        lowlevel::arguments::{Argument, Arguments, PrimitiveArgument},
        parser::common::{
            next_match, next_match_one_of, next_match_rep, parse_integer, parse_string, ParseResult,
        },
    },
    pest::{iterators::Pair, RuleType},
};

pub struct ArgumentsParser<Rule: RuleType> {
    pub argument: Rule,
    pub argument_list: Rule,
    pub integer: Rule,
    pub key_value_argument: Rule,
    pub parenthesized_argument_lists: Rule,
    pub primitive_argument: Rule,
    pub string: Rule,
}

impl<Rule: RuleType> ArgumentsParser<Rule> {
    pub fn parse_arguments(&self, arguments: Pair<'_, Rule>) -> ParseResult<Arguments, Rule> {
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

        Ok(parsed_argument_vec)
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
            Argument::PrimitiveArgument(self.parse_primitive_argument(argument_variant)?)
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
        let parsed_key = self.parse_primitive_argument(key)?;

        let value = next_match(&mut argument_elements, self.primitive_argument)?;
        let parsed_value = self.parse_primitive_argument(value)?;

        Ok(Argument::KeyValueArgument { key: parsed_key, value: parsed_value })
    }

    fn parse_primitive_argument(
        &self,
        primitive_argument: Pair<'_, Rule>,
    ) -> ParseResult<PrimitiveArgument, Rule> {
        let mut argument_elements = primitive_argument.into_inner();

        let argument_variant =
            next_match_one_of(&mut argument_elements, vec![self.string, self.integer])?;

        let argument_variant_rule = argument_variant.as_rule();
        let parsed_argument = if argument_variant_rule == self.string {
            PrimitiveArgument::String(parse_string(argument_variant)?)
        } else if argument_variant_rule == self.integer {
            PrimitiveArgument::Integer(parse_integer(argument_variant)?)
        } else {
            // This is unreachable since next_match_one_of only returns success if one of the rules
            // passed into it matches; otherwise it returns Err and this method will return early
            // before reaching this point.
            unreachable!()
        };

        Ok(parsed_argument)
    }
}
