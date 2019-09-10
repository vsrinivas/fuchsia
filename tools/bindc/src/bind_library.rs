// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// lazy_static is required for re_find_static.
use lazy_static::lazy_static;

use nom::branch::alt;
use nom::bytes::complete::{escaped, is_not, tag, take_until};
use nom::character::complete::{char, digit1, hex_digit1, multispace0, one_of};
use nom::combinator::{map, map_res, opt, value};
use nom::error::{ErrorKind, ParseError};
use nom::multi::{many0, separated_nonempty_list};
use nom::sequence::{delimited, preceded, separated_pair, terminated, tuple};
use nom::IResult;
use nom::{named, re_find_static};
use std::str::FromStr;

#[derive(Debug, PartialEq)]
pub struct CompoundIdentifier {
    namespace: Vec<String>,
    name: String,
}

#[derive(Debug, PartialEq)]
pub struct Include {
    name: CompoundIdentifier,
    alias: Option<String>,
}

#[derive(Debug, PartialEq)]
pub struct Ast {
    pub name: CompoundIdentifier,
    pub using: Vec<Include>,
    pub declarations: Vec<Declaration>,
}

#[derive(Debug, PartialEq)]
pub struct Declaration {
    pub identifier: CompoundIdentifier,
    pub extends: bool,
    pub values: Vec<Value>,
}

#[derive(Debug, PartialEq)]
pub enum Value {
    Number(String, u64),
    Str(String, String),
    Bool(String, bool),
    Enum(String),
}

#[derive(Debug, PartialEq)]
pub enum LibraryError {
    Type(String),
    StringLiteral(String),
    NumericLiteral(String),
    BoolLiteral(String),
    Identifier(String),
    Semicolon(String),
    Assignment(String),
    ListStart(String),
    ListEnd(String),
    ListSeparator(String),
    LibraryKeyword(String),
    UsingKeyword(String),
    AsKeyword(String),
    UnrecognisedInput(String),
    Unknown(String, ErrorKind),
}

impl ParseError<&str> for LibraryError {
    fn from_error_kind(input: &str, kind: ErrorKind) -> Self {
        LibraryError::Unknown(input.to_string(), kind)
    }

    fn append(input: &str, kind: ErrorKind, _: Self) -> Self {
        LibraryError::Unknown(input.to_string(), kind)
    }
}

impl FromStr for Ast {
    type Err = LibraryError;

    fn from_str(input: &str) -> Result<Self, Self::Err> {
        match library(input) {
            Ok((_, ast)) => Ok(ast),
            Err(nom::Err::Error(e)) => Err(e),
            Err(nom::Err::Failure(e)) => Err(e),
            Err(nom::Err::Incomplete(_)) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        }
    }
}

// Wraps a parser |f| and discards zero or more whitespace characters before and after it.
fn ws<I, O, E: ParseError<I>, F>(f: F) -> impl Fn(I) -> IResult<I, O, E>
where
    I: nom::InputTakeAtPosition<Item = char>,
    F: Fn(I) -> IResult<I, O, E>,
{
    delimited(multispace0, f, multispace0)
}

// Wraps a parser and replaces its error.
fn map_err<'a, O, P, G>(parser: P, f: G) -> impl Fn(&'a str) -> IResult<&'a str, O, LibraryError>
where
    P: Fn(&'a str) -> IResult<&'a str, O, (&'a str, ErrorKind)>,
    G: Fn(String) -> LibraryError,
{
    move |input: &str| {
        parser(input).map_err(|e| match e {
            nom::Err::Error((i, _)) => nom::Err::Error(f(i.to_string())),
            nom::Err::Failure((i, _)) => nom::Err::Failure(f(i.to_string())),
            nom::Err::Incomplete(_) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        })
    }
}

fn keyword_extend(input: &str) -> IResult<&str, &str, LibraryError> {
    ws(tag("extend"))(input)
}

#[derive(Clone, Debug)]
enum Type {
    Number,
    Str,
    Bool,
    Enum,
}

fn keyword_uint(input: &str) -> IResult<&str, Type> {
    value(Type::Number, ws(tag("uint")))(input)
}

fn keyword_string(input: &str) -> IResult<&str, Type> {
    value(Type::Str, ws(tag("string")))(input)
}

fn keyword_bool(input: &str) -> IResult<&str, Type> {
    value(Type::Bool, ws(tag("bool")))(input)
}

fn keyword_enum(input: &str) -> IResult<&str, Type> {
    value(Type::Enum, ws(tag("enum")))(input)
}

fn string_literal(input: &str) -> IResult<&str, String, LibraryError> {
    let escapable = escaped(is_not(r#"\""#), '\\', one_of(r#"\""#));
    let literal = delimited(char('"'), escapable, char('"'));
    map_err(map(literal, |s: &str| s.to_string()), LibraryError::StringLiteral)(input)
}

fn numeric_literal(input: &str) -> IResult<&str, u64, LibraryError> {
    let base10 = map_res(digit1, |s| u64::from_str_radix(s, 10));
    let base16 = map_res(preceded(tag("0x"), hex_digit1), |s| u64::from_str_radix(s, 16));
    // Note: When the base16 parser fails but input starts with '0x' this will succeed and return 0.
    map_err(alt((base16, base10)), LibraryError::NumericLiteral)(input)
}

fn bool_literal(input: &str) -> IResult<&str, bool, LibraryError> {
    let true_ = value(true, tag("true"));
    let false_ = value(false, tag("false"));
    map_err(alt((true_, false_)), LibraryError::BoolLiteral)(input)
}

fn identifier(input: &str) -> IResult<&str, String, LibraryError> {
    named!(matcher<&str,&str>, re_find_static!(r"^[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?"));
    map_err(map(matcher, |s| s.to_string()), LibraryError::Identifier)(input)
}

fn compound_identifier(input: &str) -> IResult<&str, CompoundIdentifier, LibraryError> {
    let (input, mut segments) = separated_nonempty_list(tag("."), identifier)(input)?;
    // Segments must be nonempty, so it's safe to pop off the name.
    let name = segments.pop().unwrap();
    Ok((input, CompoundIdentifier { namespace: segments, name }))
}

fn value_list<'a, O, F>(f: F) -> impl Fn(&'a str) -> IResult<&'a str, Vec<O>, LibraryError>
where
    F: Fn(&'a str) -> IResult<&'a str, O, LibraryError>,
{
    move |input: &'a str| {
        let separator = || map_err(ws(tag(",")), LibraryError::ListSeparator);
        let values = separated_nonempty_list(separator(), |s| f(s));

        // Lists may optionally be terminated by an additional trailing separator.
        let values = terminated(values, opt(separator()));

        // First consume all input until ';'. This simplifies the error handling since a semicolon
        // is mandatory, but a list of values is optional.
        let (input, vals_input) =
            map_err(terminated(take_until(";"), tag(";")), LibraryError::Semicolon)(input)?;

        if vals_input.is_empty() {
            return Ok((input, Vec::new()));
        }

        let list_start = map_err(tag("{"), LibraryError::ListStart);
        let list_end = map_err(tag("}"), LibraryError::ListEnd);
        let (_, result) = delimited(list_start, ws(values), list_end)(vals_input)?;

        Ok((input, result))
    }
}

fn number_value_list(input: &str) -> IResult<&str, Vec<Value>, LibraryError> {
    let token = map_err(tag("="), LibraryError::Assignment);
    let value = separated_pair(ws(identifier), token, ws(numeric_literal));
    value_list(map(value, |(ident, val)| Value::Number(ident, val)))(input)
}

fn string_value_list(input: &str) -> IResult<&str, Vec<Value>, LibraryError> {
    let token = map_err(tag("="), LibraryError::Assignment);
    let value = separated_pair(ws(identifier), token, ws(string_literal));
    value_list(map(value, |(ident, val)| Value::Str(ident, val)))(input)
}

fn bool_value_list(input: &str) -> IResult<&str, Vec<Value>, LibraryError> {
    let token = map_err(tag("="), LibraryError::Assignment);
    let value = separated_pair(ws(identifier), token, ws(bool_literal));
    value_list(map(value, |(ident, val)| Value::Bool(ident, val)))(input)
}

fn enum_value_list(input: &str) -> IResult<&str, Vec<Value>, LibraryError> {
    value_list(map(identifier, Value::Enum))(input)
}

fn declaration(input: &str) -> IResult<&str, Declaration, LibraryError> {
    let (input, extends) = opt(keyword_extend)(input)?;

    let (input, typ) = map_err(
        alt((keyword_uint, keyword_string, keyword_bool, keyword_enum)),
        LibraryError::Type,
    )(input)?;

    let (input, identifier) = ws(compound_identifier)(input)?;

    let value_parser = match typ {
        Type::Number => number_value_list,
        Type::Str => string_value_list,
        Type::Bool => bool_value_list,
        Type::Enum => enum_value_list,
    };

    let (input, vals) = value_parser(input)?;

    Ok((input, Declaration { identifier, extends: extends.is_some(), values: vals }))
}

fn using(input: &str) -> IResult<&str, Include, LibraryError> {
    let as_keyword = map_err(ws(tag("as")), LibraryError::AsKeyword);
    let (input, name) = compound_identifier(input)?;
    let (input, alias) = opt(preceded(as_keyword, identifier))(input)?;
    Ok((input, Include { name, alias }))
}

fn using_list(input: &str) -> IResult<&str, Vec<Include>, LibraryError> {
    let using_keyword = map_err(ws(tag("using")), LibraryError::UsingKeyword);
    let terminator = map_err(ws(tag(";")), LibraryError::Semicolon);
    let using = delimited(using_keyword, ws(using), terminator);
    many0(ws(using))(input)
}

fn library_name(input: &str) -> IResult<&str, CompoundIdentifier, LibraryError> {
    let keyword = map_err(ws(tag("library")), LibraryError::LibraryKeyword);
    let terminator = map_err(ws(tag(";")), LibraryError::Semicolon);
    delimited(keyword, ws(compound_identifier), terminator)(input)
}

// Reimplementation of nom::combinator::all_consuming with LibraryError error type.
fn all_consuming<'a, O, F>(f: F) -> impl Fn(&'a str) -> IResult<&'a str, O, LibraryError>
where
    F: Fn(&'a str) -> IResult<&'a str, O, LibraryError>,
{
    move |input: &'a str| {
        let (input, res) = f(input)?;
        if input.len() == 0 {
            Ok((input, res))
        } else {
            Err(nom::Err::Error(LibraryError::UnrecognisedInput(input.to_string())))
        }
    }
}

fn library(input: &str) -> IResult<&str, Ast, LibraryError> {
    map(
        all_consuming(tuple((ws(library_name), ws(using_list), many0(ws(declaration))))),
        |(name, using, declarations)| Ast { name, using, declarations },
    )(input)
}

#[cfg(test)]
mod test {
    use super::*;

    macro_rules! make_identifier {
        ( $name:tt ) => {
            {
                CompoundIdentifier {
                    namespace: Vec::new(),
                    name: String::from($name),
                }
            }
        };
        ( $namespace:tt , $($rest:tt)* ) => {
            {
                let mut identifier = make_identifier!($($rest)*);
                identifier.namespace.insert(0, String::from($namespace));
                identifier
            }
        };
    }

    mod string_literal {
        use super::*;

        #[test]
        fn basic() {
            // Matches a string literal, leaves correct tail.
            assert_eq!(string_literal(r#""abc 123"xyz"#), Ok(("xyz", "abc 123".to_string())));
        }

        #[test]
        fn match_once() {
            assert_eq!(string_literal(r#""abc""123""#), Ok((r#""123""#, "abc".to_string())));
        }

        #[test]
        fn escaped() {
            assert_eq!(
                string_literal(r#""abc \"esc\" xyz""#),
                Ok(("", r#"abc \"esc\" xyz"#.to_string()))
            );
        }

        #[test]
        fn requires_quotations() {
            assert_eq!(
                string_literal(r#"abc"#),
                Err(nom::Err::Error(LibraryError::StringLiteral("abc".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                string_literal(""),
                Err(nom::Err::Error(LibraryError::StringLiteral("".to_string())))
            );
        }
    }

    mod numeric_literals {
        use super::*;

        #[test]
        fn decimal() {
            assert_eq!(numeric_literal("123"), Ok(("", 123)));
        }

        #[test]
        fn hex() {
            assert_eq!(numeric_literal("0x123"), Ok(("", 0x123)));
            assert_eq!(numeric_literal("0xabcdef"), Ok(("", 0xabcdef)));
            assert_eq!(numeric_literal("0xABCDEF"), Ok(("", 0xabcdef)));
            assert_eq!(numeric_literal("0x123abc"), Ok(("", 0x123abc)));

            // Does not match hex without '0x' prefix.
            assert_eq!(
                numeric_literal("abc123"),
                Err(nom::Err::Error(LibraryError::NumericLiteral("abc123".to_string())))
            );
            assert_eq!(numeric_literal("123abc"), Ok(("abc", 123)));
        }

        #[test]
        fn non_numbers() {
            assert_eq!(
                numeric_literal("xyz"),
                Err(nom::Err::Error(LibraryError::NumericLiteral("xyz".to_string())))
            );

            // Does not match negative numbers (for now).
            assert_eq!(
                numeric_literal("-1"),
                Err(nom::Err::Error(LibraryError::NumericLiteral("-1".to_string())))
            );
        }

        #[test]
        fn overflow() {
            // Does not match numbers larger than u64.
            assert_eq!(numeric_literal("18446744073709551615"), Ok(("", 18446744073709551615)));
            assert_eq!(numeric_literal("0xffffffffffffffff"), Ok(("", 18446744073709551615)));
            assert_eq!(
                numeric_literal("18446744073709551616"),
                Err(nom::Err::Error(LibraryError::NumericLiteral(
                    "18446744073709551616".to_string()
                )))
            );
            // Note: This is matching '0' from '0x' but failing to parse the entire string.
            assert_eq!(numeric_literal("0x10000000000000000"), Ok(("x10000000000000000", 0)));
        }

        #[test]
        fn empty() {
            // Does not match an empty string.
            assert_eq!(
                numeric_literal(""),
                Err(nom::Err::Error(LibraryError::NumericLiteral("".to_string())))
            );
        }
    }

    mod bool_literals {
        use super::*;

        #[test]
        fn basic() {
            assert_eq!(bool_literal("true"), Ok(("", true)));
            assert_eq!(bool_literal("false"), Ok(("", false)));
        }

        #[test]
        fn non_bools() {
            // Does not match anything else.
            assert_eq!(
                bool_literal("tralse"),
                Err(nom::Err::Error(LibraryError::BoolLiteral("tralse".to_string())))
            );
        }

        #[test]
        fn empty() {
            // Does not match an empty string.
            assert_eq!(
                bool_literal(""),
                Err(nom::Err::Error(LibraryError::BoolLiteral("".to_string())))
            );
        }
    }

    mod identifiers {
        use super::*;

        #[test]
        fn basic() {
            // Matches identifiers with lowercase, uppercase, digits, and underscores.
            assert_eq!(identifier("abc_123_ABC"), Ok(("", "abc_123_ABC".to_string())));

            // Match is terminated by whitespace or punctuation.
            assert_eq!(identifier("abc_123_ABC "), Ok((" ", "abc_123_ABC".to_string())));
            assert_eq!(identifier("abc_123_ABC;"), Ok((";", "abc_123_ABC".to_string())));
        }

        #[test]
        fn invalid() {
            // Does not match an identifier beginning or ending with '_'.
            assert_eq!(
                identifier("_abc"),
                Err(nom::Err::Error(LibraryError::Identifier("_abc".to_string())))
            );

            // Note: Matches up until the '_' but fails to parse the entire string.
            assert_eq!(identifier("abc_"), Ok(("_", "abc".to_string())));
        }

        #[test]
        fn empty() {
            // Does not match an empty string.
            assert_eq!(
                identifier(""),
                Err(nom::Err::Error(LibraryError::Identifier("".to_string())))
            );
        }
    }

    mod compound_identifiers {
        use super::*;

        #[test]
        fn single_identifier() {
            // Matches single identifier.
            assert_eq!(
                compound_identifier("abc_123_ABC"),
                Ok(("", make_identifier!["abc_123_ABC"]))
            );
        }

        #[test]
        fn multiple_identifiers() {
            // Matches compound identifiers.
            assert_eq!(compound_identifier("abc.def"), Ok(("", make_identifier!["abc", "def"])));
            assert_eq!(
                compound_identifier("abc.def.ghi"),
                Ok(("", make_identifier!["abc", "def", "ghi"]))
            );
        }

        #[test]
        fn empty() {
            // Does not match empty identifiers.
            assert_eq!(
                compound_identifier("."),
                Err(nom::Err::Error(LibraryError::Identifier(".".to_string())))
            );
            assert_eq!(
                compound_identifier(".abc"),
                Err(nom::Err::Error(LibraryError::Identifier(".abc".to_string())))
            );
            assert_eq!(compound_identifier("abc..def"), Ok(("..def", make_identifier!["abc"])));
            assert_eq!(compound_identifier("abc."), Ok((".", make_identifier!["abc"])));

            // Does not match an empty string.
            assert_eq!(
                compound_identifier(""),
                Err(nom::Err::Error(LibraryError::Identifier("".to_string())))
            );
        }
    }

    mod number_value_lists {
        use super::*;

        #[test]
        fn single_value() {
            assert_eq!(
                number_value_list("{abc = 123};"),
                Ok(("", vec![Value::Number("abc".to_string(), 123)]))
            );
        }

        #[test]
        fn multiple_values() {
            // Matches multiple string values.
            assert_eq!(
                number_value_list("{abc = 123, DEF = 456};"),
                Ok((
                    "",
                    vec![
                        Value::Number("abc".to_string(), 123),
                        Value::Number("DEF".to_string(), 456)
                    ]
                ))
            );
            assert_eq!(
                number_value_list("{abc = 123, DEF = 456, ghi = 0xabc};"),
                Ok((
                    "",
                    vec![
                        Value::Number("abc".to_string(), 123),
                        Value::Number("DEF".to_string(), 456),
                        Value::Number("ghi".to_string(), 0xabc),
                    ]
                ))
            );
        }

        #[test]
        fn whitespace() {
            // Handles whitespace.
            assert_eq!(
                number_value_list("{  abc=123,\n\tDEF\t =  0xdef\n};"),
                Ok((
                    "",
                    vec![
                        Value::Number("abc".to_string(), 123),
                        Value::Number("DEF".to_string(), 0xdef)
                    ]
                ))
            );
        }

        #[test]
        fn invalid_values() {
            // Does not match non-number values.
            assert_eq!(
                number_value_list("{abc = \"string\"};"),
                Err(nom::Err::Error(LibraryError::NumericLiteral("\"string\"}".to_string())))
            );
            assert_eq!(
                number_value_list("{abc = true};"),
                Err(nom::Err::Error(LibraryError::NumericLiteral("true}".to_string())))
            );
        }

        #[test]
        fn trailing_comma() {
            // Matches trailing ','.
            assert_eq!(
                number_value_list("{abc = 123,};"),
                Ok(("", vec![Value::Number("abc".to_string(), 123)]))
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                number_value_list("{};"),
                Err(nom::Err::Error(LibraryError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                number_value_list("abc = 123};"),
                Err(nom::Err::Error(LibraryError::ListStart("abc = 123}".to_string())))
            );
            assert_eq!(
                number_value_list("{abc = 123;"),
                Err(nom::Err::Error(LibraryError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list("{abc 123};"),
                Err(nom::Err::Error(LibraryError::Assignment("123}".to_string())))
            );
        }

        #[test]
        fn no_list() {
            // Matches no list.
            assert_eq!(number_value_list(";"), Ok(("", vec![])));
        }
    }

    mod string_value_lists {
        use super::*;

        #[test]
        fn single_value() {
            assert_eq!(
                string_value_list(r#"{abc = "xyz"};"#),
                Ok(("", vec![Value::Str("abc".to_string(), "xyz".to_string())]))
            );
        }

        #[test]
        fn multiple_values() {
            // Matches multiple string values.
            assert_eq!(
                string_value_list(r#"{abc = "xyz", DEF = "UVW"};"#),
                Ok((
                    "",
                    vec![
                        Value::Str("abc".to_string(), "xyz".to_string()),
                        Value::Str("DEF".to_string(), "UVW".to_string())
                    ]
                ))
            );
            assert_eq!(
                string_value_list(r#"{abc = "xyz", DEF = "UVW", ghi = "rst"};"#),
                Ok((
                    "",
                    vec![
                        Value::Str("abc".to_string(), "xyz".to_string()),
                        Value::Str("DEF".to_string(), "UVW".to_string()),
                        Value::Str("ghi".to_string(), "rst".to_string()),
                    ]
                ))
            );
        }

        #[test]
        fn whitespace() {
            // Handles whitespace.
            assert_eq!(
                string_value_list("{  abc=\"xyz\",\n\tDEF\t =  \"UVW\"\n};"),
                Ok((
                    "",
                    vec![
                        Value::Str("abc".to_string(), "xyz".to_string()),
                        Value::Str("DEF".to_string(), "UVW".to_string())
                    ]
                ))
            );
        }

        #[test]
        fn invalid_values() {
            // Does not match non-string values.
            assert_eq!(
                string_value_list("{abc = 123};"),
                Err(nom::Err::Error(LibraryError::StringLiteral("123}".to_string())))
            );
            assert_eq!(
                string_value_list("{abc = true};"),
                Err(nom::Err::Error(LibraryError::StringLiteral("true}".to_string())))
            );
        }

        #[test]
        fn trailing_comma() {
            // Matches trailing ','.
            assert_eq!(
                string_value_list(r#"{abc = "xyz",};"#),
                Ok(("", vec![Value::Str("abc".to_string(), "xyz".to_string())]))
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                string_value_list("{};"),
                Err(nom::Err::Error(LibraryError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                string_value_list(r#"abc = "xyz"};"#),
                Err(nom::Err::Error(LibraryError::ListStart(r#"abc = "xyz"}"#.to_string())))
            );
            assert_eq!(
                string_value_list(r#"{abc = "xyz";"#),
                Err(nom::Err::Error(LibraryError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list(r#"{abc "xyz"};"#),
                Err(nom::Err::Error(LibraryError::Assignment(r#""xyz"}"#.to_string())))
            );
        }

        #[test]
        fn no_list() {
            // Matches no list.
            assert_eq!(string_value_list(";"), Ok(("", vec![])));
        }
    }

    mod bool_value_lists {
        use super::*;

        #[test]
        fn single_value() {
            // Matches one string value.
            assert_eq!(
                bool_value_list("{abc = true};"),
                Ok(("", vec![Value::Bool("abc".to_string(), true)]))
            );
        }

        #[test]
        fn multiple_values() {
            // Matches multiple string values.
            assert_eq!(
                bool_value_list("{abc = true, DEF = false};"),
                Ok((
                    "",
                    vec![
                        Value::Bool("abc".to_string(), true),
                        Value::Bool("DEF".to_string(), false)
                    ]
                ))
            );
            assert_eq!(
                bool_value_list("{abc = true, DEF = false, ghi = false};"),
                Ok((
                    "",
                    vec![
                        Value::Bool("abc".to_string(), true),
                        Value::Bool("DEF".to_string(), false),
                        Value::Bool("ghi".to_string(), false),
                    ]
                ))
            );
        }

        #[test]
        fn whitespace() {
            // Handles whitespace.
            assert_eq!(
                bool_value_list("{  abc=true,\n\tDEF\t =  false\n};"),
                Ok((
                    "",
                    vec![
                        Value::Bool("abc".to_string(), true),
                        Value::Bool("DEF".to_string(), false),
                    ]
                ))
            );
        }

        #[test]
        fn invalid_values() {
            // Does not match non-bool values.
            assert_eq!(
                bool_value_list("{abc = 123};"),
                Err(nom::Err::Error(LibraryError::BoolLiteral("123}".to_string())))
            );
            assert_eq!(
                bool_value_list("{abc = \"string\"};"),
                Err(nom::Err::Error(LibraryError::BoolLiteral("\"string\"}".to_string())))
            );
        }

        #[test]
        fn trailing_comma() {
            // Matches trailing ','.
            assert_eq!(
                bool_value_list(r#"{abc = true,};"#),
                Ok(("", vec![Value::Bool("abc".to_string(), true)]))
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                bool_value_list("{};"),
                Err(nom::Err::Error(LibraryError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                bool_value_list("abc = true};"),
                Err(nom::Err::Error(LibraryError::ListStart("abc = true}".to_string())))
            );
            assert_eq!(
                bool_value_list("{abc = true;"),
                Err(nom::Err::Error(LibraryError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list("{abc false};"),
                Err(nom::Err::Error(LibraryError::Assignment("false}".to_string())))
            );
        }

        #[test]
        fn no_list() {
            // Matches no list
            assert_eq!(bool_value_list(";"), Ok(("", vec![])));
        }
    }

    mod enum_value_lists {
        use super::*;

        #[test]
        fn single_value() {
            // Matches one identifier.
            assert_eq!(enum_value_list("{abc};"), Ok(("", vec![Value::Enum("abc".to_string())])));
        }

        #[test]
        fn multiple_values() {
            // Matches multiple identifiers.
            assert_eq!(
                enum_value_list("{abc,def};"),
                Ok(("", vec![Value::Enum("abc".to_string()), Value::Enum("def".to_string())]))
            );
            assert_eq!(
                enum_value_list("{abc,def,ghi};"),
                Ok((
                    "",
                    vec![
                        Value::Enum("abc".to_string()),
                        Value::Enum("def".to_string()),
                        Value::Enum("ghi".to_string()),
                    ]
                ))
            );
        }

        #[test]
        fn whitespace() {
            // Matches multiple identifiers with whitespace.
            assert_eq!(
                enum_value_list("{abc,   def, \tghi,\n jkl};"),
                Ok((
                    "",
                    vec![
                        Value::Enum("abc".to_string()),
                        Value::Enum("def".to_string()),
                        Value::Enum("ghi".to_string()),
                        Value::Enum("jkl".to_string()),
                    ]
                ))
            );
        }

        #[test]
        fn trailing_comma() {
            // Matches trailing ','.
            assert_eq!(enum_value_list("{abc,};"), Ok(("", vec![Value::Enum("abc".to_string())])));
        }

        #[test]
        fn no_list() {
            // Matches no list.
            assert_eq!(enum_value_list(";"), Ok(("", vec![])));
        }

        #[test]
        fn invalid_list() {
            // Must have semicolon.
            assert_eq!(
                enum_value_list("{abc,}"),
                Err(nom::Err::Error(LibraryError::Semicolon("{abc,}".to_string())))
            );

            // Must have list start and end braces.
            assert_eq!(
                enum_value_list("abc};"),
                Err(nom::Err::Error(LibraryError::ListStart("abc}".to_string())))
            );
            assert_eq!(
                enum_value_list("{abc;"),
                Err(nom::Err::Error(LibraryError::ListEnd("".to_string())))
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                enum_value_list("{};"),
                Err(nom::Err::Error(LibraryError::Identifier("}".to_string())))
            );
        }
    }

    mod declarations {
        use super::*;

        #[test]
        fn no_value() {
            // Matches key declaration without values.
            assert_eq!(
                declaration("uint test;"),
                Ok((
                    "",
                    Declaration {
                        identifier: make_identifier!["test"],
                        extends: false,
                        values: vec![],
                    }
                ))
            );
        }

        #[test]
        fn numbers() {
            // Matches numbers.
            assert_eq!(
                declaration("uint test { x = 1 };"),
                Ok((
                    "",
                    Declaration {
                        identifier: make_identifier!["test"],
                        extends: false,
                        values: vec![Value::Number("x".to_string(), 1)],
                    }
                ))
            );
        }

        #[test]
        fn strings() {
            // Matches strings.
            assert_eq!(
                declaration(r#"string test { x = "a" };"#),
                Ok((
                    "",
                    Declaration {
                        identifier: make_identifier!["test"],
                        extends: false,
                        values: vec![Value::Str("x".to_string(), "a".to_string())],
                    }
                ))
            );
        }

        #[test]
        fn bools() {
            // Matches bools.
            assert_eq!(
                declaration("bool test { x = false };"),
                Ok((
                    "",
                    Declaration {
                        identifier: make_identifier!["test"],
                        extends: false,
                        values: vec![Value::Bool("x".to_string(), false)],
                    }
                ))
            );
        }

        #[test]
        fn enums() {
            // Matches enums.
            assert_eq!(
                declaration("enum test { x };"),
                Ok((
                    "",
                    Declaration {
                        identifier: make_identifier!["test"],
                        extends: false,
                        values: vec![Value::Enum("x".to_string())],
                    }
                ))
            );
        }

        #[test]
        fn extend() {
            // Handles "extend" keyword.
            assert_eq!(
                declaration("extend uint test { x = 1 };"),
                Ok((
                    "",
                    Declaration {
                        identifier: make_identifier!["test"],
                        extends: true,
                        values: vec![Value::Number("x".to_string(), 1)],
                    }
                ))
            );
        }

        #[test]
        fn type_mismatch() {
            // Handles type mismatches.
            assert_eq!(
                declaration("uint test { x = false };"),
                Err(nom::Err::Error(LibraryError::NumericLiteral("false }".to_string())))
            );
        }

        #[test]
        fn invalid() {
            // Must have a type, and an identifier.
            assert_eq!(
                declaration("bool { x = false };"),
                Err(nom::Err::Error(LibraryError::Identifier("{ x = false };".to_string())))
            );
            assert_eq!(
                declaration("test { x = false };"),
                Err(nom::Err::Error(LibraryError::Type("test { x = false };".to_string())))
            );

            // Must be terminated by ';'.
            assert_eq!(
                declaration("bool test { x = false }"),
                Err(nom::Err::Error(LibraryError::Semicolon("{ x = false }".to_string())))
            );
        }

        #[test]
        fn compound_identifier() {
            // Identifier can be compound.
            assert_eq!(
                declaration("uint this.is.a.test { x = 1 };"),
                Ok((
                    "",
                    Declaration {
                        identifier: make_identifier!["this", "is", "a", "test"],
                        extends: false,
                        values: vec![Value::Number("x".to_string(), 1)],
                    }
                ))
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(declaration(""), Err(nom::Err::Error(LibraryError::Type("".to_string()))));
        }
    }

    mod using_lists {
        use super::*;

        #[test]
        fn one_include() {
            assert_eq!(
                using_list("using test;"),
                Ok(("", vec![Include { name: make_identifier!["test"], alias: None }]))
            );
        }

        #[test]
        fn multiple_includes() {
            assert_eq!(
                using_list("using abc;using def;"),
                Ok((
                    "",
                    vec![
                        Include { name: make_identifier!["abc"], alias: None },
                        Include { name: make_identifier!["def"], alias: None },
                    ]
                ))
            );
            assert_eq!(
                using_list("using abc;using def;using ghi;"),
                Ok((
                    "",
                    vec![
                        Include { name: make_identifier!["abc"], alias: None },
                        Include { name: make_identifier!["def"], alias: None },
                        Include { name: make_identifier!["ghi"], alias: None },
                    ]
                ))
            );
        }

        #[test]
        fn compound_identifiers() {
            assert_eq!(
                using_list("using abc.def;"),
                Ok(("", vec![Include { name: make_identifier!["abc", "def"], alias: None }]))
            );
        }

        #[test]
        fn aliases() {
            assert_eq!(
                using_list("using abc.def as one;using ghi as two;"),
                Ok((
                    "",
                    vec![
                        Include {
                            name: make_identifier!["abc", "def"],
                            alias: Some("one".to_string()),
                        },
                        Include { name: make_identifier!["ghi"], alias: Some("two".to_string()) },
                    ]
                ))
            );
        }

        #[test]
        fn whitespace() {
            assert_eq!(
                using_list(" using   abc\t as  one  ;\n using def ; "),
                Ok((
                    "",
                    vec![
                        Include { name: make_identifier!["abc"], alias: Some("one".to_string()) },
                        Include { name: make_identifier!["def"], alias: None },
                    ]
                ))
            );
            assert_eq!(
                using_list("usingabc;"),
                Ok(("", vec![Include { name: make_identifier!["abc"], alias: None }]))
            );
        }

        #[test]
        fn invalid() {
            // Must be followed by ';'.
            assert_eq!(using_list("using abc"), Ok(("using abc", vec![])));
        }

        #[test]
        fn empty() {
            assert_eq!(using_list(""), Ok(("", vec![])));
        }
    }

    mod library_names {
        use super::*;

        #[test]
        fn single_name() {
            assert_eq!(library_name("library a;"), Ok(("", make_identifier!["a"])));
        }

        #[test]
        fn compound_name() {
            assert_eq!(library_name("library a.b;"), Ok(("", make_identifier!["a", "b"])));
        }

        #[test]
        fn whitespace() {
            assert_eq!(library_name("library \n\t a\n\t ;"), Ok(("", make_identifier!["a"])));
        }

        #[test]
        fn invalid() {
            // Must have a name.
            assert_eq!(
                library_name("library ;"),
                Err(nom::Err::Error(LibraryError::Identifier(";".to_string())))
            );

            // Must be terminated by ';'.
            assert_eq!(
                library_name("library a"),
                Err(nom::Err::Error(LibraryError::Semicolon("".to_string())))
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                library_name(""),
                Err(nom::Err::Error(LibraryError::LibraryKeyword("".to_string())))
            );
        }
    }

    mod libraries {
        use super::*;

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                library(""),
                Err(nom::Err::Error(LibraryError::LibraryKeyword("".to_string())))
            );
        }

        #[test]
        fn empty_library() {
            assert_eq!(
                library("library a;"),
                Ok(("", Ast { name: make_identifier!["a"], using: vec![], declarations: vec![] }))
            );
        }

        #[test]
        fn using_list() {
            assert_eq!(
                library("library a; using c as d;"),
                Ok((
                    "",
                    Ast {
                        name: make_identifier!["a"],
                        using: vec![Include {
                            name: make_identifier!["c"],
                            alias: Some("d".to_string()),
                        }],
                        declarations: vec![]
                    }
                ))
            );
        }

        #[test]
        fn declarations() {
            assert_eq!(
                library("library a; uint t { x = 1 };"),
                Ok((
                    "",
                    Ast {
                        name: make_identifier!["a"],
                        using: vec![],
                        declarations: vec![Declaration {
                            identifier: make_identifier!["t"],
                            extends: false,
                            values: vec![(Value::Number("x".to_string(), 1))],
                        }]
                    }
                ))
            );
        }

        #[test]
        fn multiple_elements() {
            // Matches library with using list and declarations.
            assert_eq!(
                library("library a; using b.c as d; extend enum d.t { x };"),
                Ok((
                    "",
                    Ast {
                        name: make_identifier!["a"],
                        using: vec![Include {
                            name: make_identifier!["b", "c"],
                            alias: Some("d".to_string()),
                        }],
                        declarations: vec![Declaration {
                            identifier: make_identifier!["d", "t"],
                            extends: true,
                            values: vec![Value::Enum("x".to_string())],
                        }]
                    }
                ))
            );

            // Matches library with using list and two declarations.
            assert_eq!(
                library("library a; using b.c as d; extend enum d.t { x }; bool e;"),
                Ok((
                    "",
                    Ast {
                        name: make_identifier!["a"],
                        using: vec![Include {
                            name: make_identifier!["b", "c"],
                            alias: Some("d".to_string()),
                        }],
                        declarations: vec![
                            Declaration {
                                identifier: make_identifier!["d", "t"],
                                extends: true,
                                values: vec![Value::Enum("x".to_string())],
                            },
                            Declaration {
                                identifier: make_identifier!["e"],
                                extends: false,
                                values: vec![],
                            }
                        ]
                    }
                ))
            );
        }

        #[test]
        fn consumes_entire_input() {
            // Must parse entire input.
            assert_eq!(
                library("library a; using b.c as d; invalid input"),
                Err(nom::Err::Error(LibraryError::UnrecognisedInput("invalid input".to_string())))
            );
        }

        #[test]
        fn whitespace() {
            // Handles whitespace.
            assert_eq!(
                library("\n\t library a;\t using b.c as d;\n extend enum d.t { x }; \t bool e;\n "),
                library("library a; using b.c as d; extend enum d.t { x }; bool e;"),
            );
        }
    }
}
