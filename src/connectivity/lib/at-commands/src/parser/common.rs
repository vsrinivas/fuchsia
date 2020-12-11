// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The functions in this module are common for parsing AT commands and responses and the
/// arguments contained in them.
///
/// The next_* functions take a Pairs, an iterator over all the child syntax
/// nodes of a given syntactic element, and check to make sure the next element is of the
/// correct type or Rule.  The rest of the functions recursively convert untyped pest trees to
/// the appropriate typed definition types.
///
/// Additionally, there are functions for parsing common scalars such as integers, strings and
/// command names.
use {
    pest::{
        error::Error as PestError,
        iterators::{Pair, Pairs},
        RuleType,
    },
    std::num::ParseIntError,
    thiserror::Error,
};

#[derive(Debug, Error)]
pub enum ParseError<Rule: RuleType> {
    #[error("Unable to use pest to parse {string:?}: {pest_error:?}")]
    PestParseFailure { string: String, pest_error: PestError<Rule> },
    #[error("Unable to parse \"{string:?}\".  Expected one of {expected_rules:?}, got nothing.")]
    NextRuleMissing { string: String, expected_rules: Vec<Rule> },
    #[error(
        "Unable to parse \"{string:?}\".  Expected one of {expected_rules:?}, got {actual_rule:?}."
    )]
    NextRuleUnexpected { string: String, expected_rules: Vec<Rule>, actual_rule: Rule },
    #[error("Unable to parse \"{string:?}\" as an integer: {error:?}.")]
    InvalidInteger { string: String, error: ParseIntError },
    #[error("Unknown character after AT: \"{0}\"")]
    UnknownExtensionCharacter(String),
}

pub type ParseResult<T, R> = std::result::Result<T, ParseError<R>>;

/// Get the next Pair syntax node out of an iterator if it is matched by one of the expected Rules.
/// Otherwise, fail.
pub fn next_match_one_of<'a, Rule: RuleType>(
    pairs: &mut Pairs<'a, Rule>,
    expected_rules: Vec<Rule>,
) -> ParseResult<Pair<'a, Rule>, Rule> {
    let pair_result = pairs.next();
    let pair = pair_result.ok_or(ParseError::NextRuleMissing {
        string: pairs.as_str().to_string(),
        expected_rules: expected_rules.clone(),
    })?;

    let pair_rule = pair.as_rule();
    if !expected_rules.contains(&pair_rule) {
        return Err(ParseError::NextRuleUnexpected {
            string: pairs.as_str().to_string(),
            expected_rules: expected_rules.clone(),
            actual_rule: pair_rule,
        });
    }

    Ok(pair)
}

/// Get the next Pair syntax node out of an iterator if it is matched by the expected Rule.
/// Otherwise, fail.
pub fn next_match<'a, Rule: RuleType>(
    pairs: &mut Pairs<'a, Rule>,
    expected_rule: Rule,
) -> ParseResult<Pair<'a, Rule>, Rule> {
    next_match_one_of(pairs, vec![expected_rule])
}

/// Get the next Pair syntax node out of an interator if it exists.  If it doesn't exist, return
/// None.  If it does exist and is not matched by the specified Rule, fail.
pub fn next_match_option<'a, Rule: RuleType>(
    pairs: &mut Pairs<'a, Rule>,
    expected_rule: Rule,
) -> ParseResult<Option<Pair<'a, Rule>>, Rule> {
    if !pairs.peek().is_some() {
        Ok(None)
    } else {
        next_match(pairs, expected_rule).map(|pair| Some(pair))
    }
}

/// Continue getting the next Pair syntax node of the iterator as long as they are  matched by the
/// expected Rule.  Return a vector of matching Pairs.
pub fn next_match_rep<'a, Rule: RuleType>(
    pairs: &mut Pairs<'a, Rule>,
    expected_rule: Rule,
) -> Vec<Pair<'a, Rule>> {
    let is_correct_rule = |pair_option: Option<Pair<'a, Rule>>| match pair_option {
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

/// Parse a pair to a string.  The caller must ensure that this is actually matched by a
/// rule that can be parsed to a valid AT command string.
pub fn parse_string<Rule: RuleType>(string: Pair<'_, Rule>) -> ParseResult<String, Rule> {
    Ok(string.as_span().as_str().to_string())
}

/// Parse a pair to a string.  The caller must ensure that this is actually matched by a
/// rule that can be parsed to a valid integer.
pub fn parse_integer<Rule: RuleType>(integer: Pair<'_, Rule>) -> ParseResult<i64, Rule> {
    let str = integer.as_span().as_str();
    str.parse().map_err(|err| ParseError::InvalidInteger { string: str.to_string(), error: err })
}

/// Parse a pair to a string.  The caller must ensure that this is actually matched by a
/// rule that can be parsed to a valid AT command name.
pub fn parse_name<Rule: RuleType>(name: Pair<'_, Rule>) -> ParseResult<String, Rule> {
    Ok(name.as_span().as_str().to_string())
}
