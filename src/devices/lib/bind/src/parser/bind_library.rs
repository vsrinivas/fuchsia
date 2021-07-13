// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::parser::common::{
    bool_literal, compound_identifier, identifier, many_until_eof, map_err, numeric_literal,
    string_literal, using_list, ws, BindParserError, CompoundIdentifier, Include, NomSpan,
};
use nom::{
    branch::alt,
    bytes::complete::{tag, take_until},
    combinator::{map, opt, value},
    multi::separated_nonempty_list,
    sequence::{delimited, separated_pair, terminated, tuple},
    IResult,
};
use std::convert::TryFrom;

#[derive(Debug, PartialEq)]
pub struct Ast {
    pub name: CompoundIdentifier,
    pub using: Vec<Include>,
    pub declarations: Vec<Declaration>,
}

#[derive(Debug, PartialEq)]
pub struct Declaration {
    pub identifier: CompoundIdentifier,
    pub value_type: ValueType,
    pub extends: bool,
    pub values: Vec<Value>,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
pub enum ValueType {
    Number,
    Str,
    Bool,
    Enum,
}

#[derive(Debug, PartialEq)]
pub enum Value {
    Number(String, u64),
    Str(String, String),
    Bool(String, bool),
    Enum(String),
}

impl TryFrom<&str> for Ast {
    type Error = BindParserError;

    fn try_from(input: &str) -> Result<Self, Self::Error> {
        match library(NomSpan::new(input)) {
            Ok((_, ast)) => Ok(ast),
            Err(nom::Err::Error(e)) => Err(e),
            Err(nom::Err::Failure(e)) => Err(e),
            Err(nom::Err::Incomplete(_)) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        }
    }
}

impl Value {
    pub fn identifier(&self) -> &str {
        match self {
            Value::Number(identifier, _)
            | Value::Str(identifier, _)
            | Value::Bool(identifier, _)
            | Value::Enum(identifier) => &identifier,
        }
    }
}

fn keyword_extend(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(tag("extend"))(input)
}

fn keyword_uint(input: NomSpan) -> IResult<NomSpan, ValueType, BindParserError> {
    value(ValueType::Number, ws(map_err(tag("uint"), BindParserError::Type)))(input)
}

fn keyword_string(input: NomSpan) -> IResult<NomSpan, ValueType, BindParserError> {
    value(ValueType::Str, ws(map_err(tag("string"), BindParserError::Type)))(input)
}

fn keyword_bool(input: NomSpan) -> IResult<NomSpan, ValueType, BindParserError> {
    value(ValueType::Bool, ws(map_err(tag("bool"), BindParserError::Type)))(input)
}

fn keyword_enum(input: NomSpan) -> IResult<NomSpan, ValueType, BindParserError> {
    value(ValueType::Enum, ws(map_err(tag("enum"), BindParserError::Type)))(input)
}

fn value_list<'a, O, F>(
    f: F,
) -> impl Fn(NomSpan<'a>) -> IResult<NomSpan<'a>, Vec<O>, BindParserError>
where
    F: Fn(NomSpan<'a>) -> IResult<NomSpan<'a>, O, BindParserError>,
{
    move |input: NomSpan<'a>| {
        let separator = || ws(map_err(tag(","), BindParserError::ListSeparator));
        let values = separated_nonempty_list(separator(), |s| f(s));

        // Lists may optionally be terminated by an additional trailing separator.
        let values = terminated(values, opt(separator()));

        // First consume all input until ';'. This simplifies the error handling since a semicolon
        // is mandatory, but a list of values is optional.
        let (input, vals_input) =
            map_err(terminated(take_until(";"), tag(";")), BindParserError::Semicolon)(input)?;

        if vals_input.fragment().is_empty() {
            return Ok((input, Vec::new()));
        }

        let list_start = map_err(tag("{"), BindParserError::ListStart);
        let list_end = map_err(tag("}"), BindParserError::ListEnd);
        let (_, result) = delimited(ws(list_start), ws(values), ws(list_end))(vals_input)?;

        Ok((input, result))
    }
}

fn number_value_list(input: NomSpan) -> IResult<NomSpan, Vec<Value>, BindParserError> {
    let token = map_err(tag("="), BindParserError::Assignment);
    let value = separated_pair(ws(identifier), ws(token), ws(numeric_literal));
    value_list(map(value, |(ident, val)| Value::Number(ident, val)))(input)
}

fn string_value_list(input: NomSpan) -> IResult<NomSpan, Vec<Value>, BindParserError> {
    let token = map_err(tag("="), BindParserError::Assignment);
    let value = separated_pair(ws(identifier), ws(token), ws(string_literal));
    value_list(map(value, |(ident, val)| Value::Str(ident, val)))(input)
}

fn bool_value_list(input: NomSpan) -> IResult<NomSpan, Vec<Value>, BindParserError> {
    let token = map_err(tag("="), BindParserError::Assignment);
    let value = separated_pair(ws(identifier), ws(token), ws(bool_literal));
    value_list(map(value, |(ident, val)| Value::Bool(ident, val)))(input)
}

fn enum_value_list(input: NomSpan) -> IResult<NomSpan, Vec<Value>, BindParserError> {
    value_list(map(ws(identifier), Value::Enum))(input)
}

fn declaration(input: NomSpan) -> IResult<NomSpan, Declaration, BindParserError> {
    let (input, extends) = opt(keyword_extend)(input)?;

    let (input, value_type) =
        alt((keyword_uint, keyword_string, keyword_bool, keyword_enum))(input)?;

    let (input, identifier) = ws(compound_identifier)(input)?;

    let value_parser = match value_type {
        ValueType::Number => number_value_list,
        ValueType::Str => string_value_list,
        ValueType::Bool => bool_value_list,
        ValueType::Enum => enum_value_list,
    };

    let (input, vals) = value_parser(input)?;

    Ok((input, Declaration { identifier, value_type, extends: extends.is_some(), values: vals }))
}

fn library_name(input: NomSpan) -> IResult<NomSpan, CompoundIdentifier, BindParserError> {
    let keyword = ws(map_err(tag("library"), BindParserError::LibraryKeyword));
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    delimited(keyword, ws(compound_identifier), terminator)(input)
}

fn library(input: NomSpan) -> IResult<NomSpan, Ast, BindParserError> {
    map(
        tuple((ws(library_name), ws(using_list), many_until_eof(ws(declaration)))),
        |(name, using, declarations)| Ast { name, using, declarations },
    )(input)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;
    use crate::parser::common::test::check_result;

    mod number_value_lists {
        use super::*;

        #[test]
        fn single_value() {
            check_result(
                number_value_list(NomSpan::new("{abc = 123};")),
                "",
                vec![Value::Number("abc".to_string(), 123)],
            );
        }

        #[test]
        fn multiple_values() {
            // Matches multiple string values.
            check_result(
                number_value_list(NomSpan::new("{abc = 123, DEF = 456};")),
                "",
                vec![Value::Number("abc".to_string(), 123), Value::Number("DEF".to_string(), 456)],
            );
            check_result(
                number_value_list(NomSpan::new("{abc = 123, DEF = 456, ghi = 0xabc};")),
                "",
                vec![
                    Value::Number("abc".to_string(), 123),
                    Value::Number("DEF".to_string(), 456),
                    Value::Number("ghi".to_string(), 0xabc),
                ],
            );
        }

        #[test]
        fn whitespace() {
            // Handles whitespace.
            check_result(
                number_value_list(NomSpan::new("{  abc=123,\n\tDEF\t =  0xdef\n};")),
                "",
                vec![
                    Value::Number("abc".to_string(), 123),
                    Value::Number("DEF".to_string(), 0xdef),
                ],
            );
        }

        #[test]
        fn invalid_values() {
            // Does not match non-number values.
            assert_eq!(
                number_value_list(NomSpan::new("{abc = \"string\"};")),
                Err(nom::Err::Error(BindParserError::NumericLiteral("\"string\"}".to_string())))
            );
            assert_eq!(
                number_value_list(NomSpan::new("{abc = true};")),
                Err(nom::Err::Error(BindParserError::NumericLiteral("true}".to_string())))
            );
        }

        #[test]
        fn trailing_comma() {
            // Matches trailing ','.
            check_result(
                number_value_list(NomSpan::new("{abc = 123,};")),
                "",
                vec![Value::Number("abc".to_string(), 123)],
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                number_value_list(NomSpan::new("{};")),
                Err(nom::Err::Error(BindParserError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                number_value_list(NomSpan::new("abc = 123};")),
                Err(nom::Err::Error(BindParserError::ListStart("abc = 123}".to_string())))
            );
            assert_eq!(
                number_value_list(NomSpan::new("{abc = 123;")),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list(NomSpan::new("{abc 123};")),
                Err(nom::Err::Error(BindParserError::Assignment("123}".to_string())))
            );
        }

        #[test]
        fn no_list() {
            // Matches no list.
            check_result(number_value_list(NomSpan::new(";")), "", vec![]);
        }
    }

    mod string_value_lists {
        use super::*;

        #[test]
        fn single_value() {
            check_result(
                string_value_list(NomSpan::new(r#"{abc = "xyz"};"#)),
                "",
                vec![Value::Str("abc".to_string(), "xyz".to_string())],
            );
        }

        #[test]
        fn multiple_values() {
            // Matches multiple string values.
            check_result(
                string_value_list(NomSpan::new(r#"{abc = "xyz", DEF = "UVW"};"#)),
                "",
                vec![
                    Value::Str("abc".to_string(), "xyz".to_string()),
                    Value::Str("DEF".to_string(), "UVW".to_string()),
                ],
            );
            check_result(
                string_value_list(NomSpan::new(r#"{abc = "xyz", DEF = "UVW", ghi = "rst"};"#)),
                "",
                vec![
                    Value::Str("abc".to_string(), "xyz".to_string()),
                    Value::Str("DEF".to_string(), "UVW".to_string()),
                    Value::Str("ghi".to_string(), "rst".to_string()),
                ],
            );
        }

        #[test]
        fn whitespace() {
            // Handles whitespace.
            check_result(
                string_value_list(NomSpan::new("{  abc=\"xyz\",\n\tDEF\t =  \"UVW\"\n};")),
                "",
                vec![
                    Value::Str("abc".to_string(), "xyz".to_string()),
                    Value::Str("DEF".to_string(), "UVW".to_string()),
                ],
            );
        }

        #[test]
        fn invalid_values() {
            // Does not match non-string values.
            assert_eq!(
                string_value_list(NomSpan::new("{abc = 123};")),
                Err(nom::Err::Error(BindParserError::StringLiteral("123}".to_string())))
            );
            assert_eq!(
                string_value_list(NomSpan::new("{abc = true};")),
                Err(nom::Err::Error(BindParserError::StringLiteral("true}".to_string())))
            );
        }

        #[test]
        fn trailing_comma() {
            // Matches trailing ','.
            check_result(
                string_value_list(NomSpan::new(r#"{abc = "xyz",};"#)),
                "",
                vec![Value::Str("abc".to_string(), "xyz".to_string())],
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                string_value_list(NomSpan::new("{};")),
                Err(nom::Err::Error(BindParserError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                string_value_list(NomSpan::new(r#"abc = "xyz"};"#)),
                Err(nom::Err::Error(BindParserError::ListStart(r#"abc = "xyz"}"#.to_string())))
            );
            assert_eq!(
                string_value_list(NomSpan::new(r#"{abc = "xyz";"#)),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list(NomSpan::new(r#"{abc "xyz"};"#)),
                Err(nom::Err::Error(BindParserError::Assignment(r#""xyz"}"#.to_string())))
            );
        }

        #[test]
        fn no_list() {
            // Matches no list.
            check_result(string_value_list(NomSpan::new(";")), "", vec![]);
        }
    }

    mod bool_value_lists {
        use super::*;

        #[test]
        fn single_value() {
            // Matches one string value.
            check_result(
                bool_value_list(NomSpan::new("{abc = true};")),
                "",
                vec![Value::Bool("abc".to_string(), true)],
            );
        }

        #[test]
        fn multiple_values() {
            // Matches multiple string values.
            check_result(
                bool_value_list(NomSpan::new("{abc = true, DEF = false};")),
                "",
                vec![Value::Bool("abc".to_string(), true), Value::Bool("DEF".to_string(), false)],
            );
            check_result(
                bool_value_list(NomSpan::new("{abc = true, DEF = false, ghi = false};")),
                "",
                vec![
                    Value::Bool("abc".to_string(), true),
                    Value::Bool("DEF".to_string(), false),
                    Value::Bool("ghi".to_string(), false),
                ],
            );
        }

        #[test]
        fn whitespace() {
            // Handles whitespace.
            check_result(
                bool_value_list(NomSpan::new("{  abc=true,\n\tDEF\t =  false\n};")),
                "",
                vec![Value::Bool("abc".to_string(), true), Value::Bool("DEF".to_string(), false)],
            );
        }

        #[test]
        fn invalid_values() {
            // Does not match non-bool values.
            assert_eq!(
                bool_value_list(NomSpan::new("{abc = 123};")),
                Err(nom::Err::Error(BindParserError::BoolLiteral("123}".to_string())))
            );
            assert_eq!(
                bool_value_list(NomSpan::new("{abc = \"string\"};")),
                Err(nom::Err::Error(BindParserError::BoolLiteral("\"string\"}".to_string())))
            );
        }

        #[test]
        fn trailing_comma() {
            // Matches trailing ','.
            check_result(
                bool_value_list(NomSpan::new(r#"{abc = true,};"#)),
                "",
                vec![Value::Bool("abc".to_string(), true)],
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                bool_value_list(NomSpan::new("{};")),
                Err(nom::Err::Error(BindParserError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                bool_value_list(NomSpan::new("abc = true};")),
                Err(nom::Err::Error(BindParserError::ListStart("abc = true}".to_string())))
            );
            assert_eq!(
                bool_value_list(NomSpan::new("{abc = true;")),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list(NomSpan::new("{abc false};")),
                Err(nom::Err::Error(BindParserError::Assignment("false}".to_string())))
            );
        }

        #[test]
        fn no_list() {
            // Matches no list
            check_result(bool_value_list(NomSpan::new(";")), "", vec![]);
        }
    }

    mod enum_value_lists {
        use super::*;

        #[test]
        fn single_value() {
            // Matches one identifier.
            check_result(
                enum_value_list(NomSpan::new("{abc};")),
                "",
                vec![Value::Enum("abc".to_string())],
            );
        }

        #[test]
        fn multiple_values() {
            // Matches multiple identifiers.
            check_result(
                enum_value_list(NomSpan::new("{abc,def};")),
                "",
                vec![Value::Enum("abc".to_string()), Value::Enum("def".to_string())],
            );
            check_result(
                enum_value_list(NomSpan::new("{abc,def,ghi};")),
                "",
                vec![
                    Value::Enum("abc".to_string()),
                    Value::Enum("def".to_string()),
                    Value::Enum("ghi".to_string()),
                ],
            );
        }

        #[test]
        fn whitespace() {
            // Matches multiple identifiers with whitespace.
            check_result(
                enum_value_list(NomSpan::new("{abc,   def, \tghi,\n jkl};")),
                "",
                vec![
                    Value::Enum("abc".to_string()),
                    Value::Enum("def".to_string()),
                    Value::Enum("ghi".to_string()),
                    Value::Enum("jkl".to_string()),
                ],
            );
        }

        #[test]
        fn trailing_comma() {
            // Matches trailing ','.
            check_result(
                enum_value_list(NomSpan::new("{abc,};")),
                "",
                vec![Value::Enum("abc".to_string())],
            );
        }

        #[test]
        fn no_list() {
            // Matches no list.
            check_result(enum_value_list(NomSpan::new(";")), "", vec![]);
        }

        #[test]
        fn invalid_list() {
            // Must have semicolon.
            assert_eq!(
                enum_value_list(NomSpan::new("{abc,}")),
                Err(nom::Err::Error(BindParserError::Semicolon("{abc,}".to_string())))
            );

            // Must have list start and end braces.
            assert_eq!(
                enum_value_list(NomSpan::new("abc};")),
                Err(nom::Err::Error(BindParserError::ListStart("abc}".to_string())))
            );
            assert_eq!(
                enum_value_list(NomSpan::new("{abc;")),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                enum_value_list(NomSpan::new("{};")),
                Err(nom::Err::Error(BindParserError::Identifier("}".to_string())))
            );
        }
    }

    mod declarations {
        use super::*;

        #[test]
        fn no_value() {
            // Matches key declaration without values.
            check_result(
                declaration(NomSpan::new("uint test;")),
                "",
                Declaration {
                    identifier: make_identifier!["test"],
                    value_type: ValueType::Number,
                    extends: false,
                    values: vec![],
                },
            );
        }

        #[test]
        fn numbers() {
            // Matches numbers.
            check_result(
                declaration(NomSpan::new("uint test { x = 1 };")),
                "",
                Declaration {
                    identifier: make_identifier!["test"],
                    value_type: ValueType::Number,
                    extends: false,
                    values: vec![Value::Number("x".to_string(), 1)],
                },
            );
        }

        #[test]
        fn strings() {
            // Matches strings.
            check_result(
                declaration(NomSpan::new(r#"string test { x = "a" };"#)),
                "",
                Declaration {
                    identifier: make_identifier!["test"],
                    value_type: ValueType::Str,
                    extends: false,
                    values: vec![Value::Str("x".to_string(), "a".to_string())],
                },
            );
        }

        #[test]
        fn bools() {
            // Matches bools.
            check_result(
                declaration(NomSpan::new("bool test { x = false };")),
                "",
                Declaration {
                    identifier: make_identifier!["test"],
                    value_type: ValueType::Bool,
                    extends: false,
                    values: vec![Value::Bool("x".to_string(), false)],
                },
            );
        }

        #[test]
        fn enums() {
            // Matches enums.
            check_result(
                declaration(NomSpan::new("enum test { x };")),
                "",
                Declaration {
                    identifier: make_identifier!["test"],
                    value_type: ValueType::Enum,
                    extends: false,
                    values: vec![Value::Enum("x".to_string())],
                },
            );
        }

        #[test]
        fn extend() {
            // Handles "extend" keyword.
            check_result(
                declaration(NomSpan::new("extend uint test { x = 1 };")),
                "",
                Declaration {
                    identifier: make_identifier!["test"],
                    value_type: ValueType::Number,
                    extends: true,
                    values: vec![Value::Number("x".to_string(), 1)],
                },
            );
        }

        #[test]
        fn type_mismatch() {
            // Handles type mismatches.
            assert_eq!(
                declaration(NomSpan::new("uint test { x = false };")),
                Err(nom::Err::Error(BindParserError::NumericLiteral("false }".to_string())))
            );
        }

        #[test]
        fn invalid() {
            // Must have a type, and an identifier.
            assert_eq!(
                declaration(NomSpan::new("bool { x = false };")),
                Err(nom::Err::Error(BindParserError::Identifier("{ x = false };".to_string())))
            );
            assert_eq!(
                declaration(NomSpan::new("test { x = false };")),
                Err(nom::Err::Error(BindParserError::Type("test { x = false };".to_string())))
            );

            // Must be terminated by ';'.
            assert_eq!(
                declaration(NomSpan::new("bool test { x = false }")),
                Err(nom::Err::Error(BindParserError::Semicolon(" { x = false }".to_string())))
            );
        }

        #[test]
        fn compound_identifier() {
            // Identifier can be compound.
            check_result(
                declaration(NomSpan::new("uint this.is.a.test { x = 1 };")),
                "",
                Declaration {
                    identifier: make_identifier!["this", "is", "a", "test"],
                    value_type: ValueType::Number,
                    extends: false,
                    values: vec![Value::Number("x".to_string(), 1)],
                },
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                declaration(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::Type("".to_string())))
            );
        }
    }

    mod library_names {
        use super::*;

        #[test]
        fn single_name() {
            check_result(library_name(NomSpan::new("library a;")), "", make_identifier!["a"]);
        }

        #[test]
        fn compound_name() {
            check_result(
                library_name(NomSpan::new("library a.b;")),
                "",
                make_identifier!["a", "b"],
            );
        }

        #[test]
        fn whitespace() {
            check_result(
                library_name(NomSpan::new("library \n\t a\n\t ;")),
                "",
                make_identifier!["a"],
            );
        }

        #[test]
        fn invalid() {
            // Must have a name.
            assert_eq!(
                library_name(NomSpan::new("library ;")),
                Err(nom::Err::Error(BindParserError::Identifier(";".to_string())))
            );

            // Must be terminated by ';'.
            assert_eq!(
                library_name(NomSpan::new("library a")),
                Err(nom::Err::Error(BindParserError::Semicolon("".to_string())))
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                library_name(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::LibraryKeyword("".to_string())))
            );
        }
    }

    mod libraries {
        use super::*;

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                library(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::LibraryKeyword("".to_string())))
            );
        }

        #[test]
        fn empty_library() {
            check_result(
                library(NomSpan::new("library a;")),
                "",
                Ast { name: make_identifier!["a"], using: vec![], declarations: vec![] },
            );
        }

        #[test]
        fn using_list() {
            check_result(
                library(NomSpan::new("library a; using c as d;")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![Include {
                        name: make_identifier!["c"],
                        alias: Some("d".to_string()),
                    }],
                    declarations: vec![],
                },
            );
        }

        #[test]
        fn declarations() {
            check_result(
                library(NomSpan::new("library a; uint t { x = 1 };")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![],
                    declarations: vec![Declaration {
                        identifier: make_identifier!["t"],
                        value_type: ValueType::Number,
                        extends: false,
                        values: vec![(Value::Number("x".to_string(), 1))],
                    }],
                },
            );
        }

        #[test]
        fn multiple_elements() {
            // Matches library with using list and declarations.
            check_result(
                library(NomSpan::new("library a; using b.c as d; extend enum d.t { x };")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![Include {
                        name: make_identifier!["b", "c"],
                        alias: Some("d".to_string()),
                    }],
                    declarations: vec![Declaration {
                        identifier: make_identifier!["d", "t"],
                        value_type: ValueType::Enum,
                        extends: true,
                        values: vec![Value::Enum("x".to_string())],
                    }],
                },
            );

            // Matches library with using list and two declarations.
            check_result(
                library(NomSpan::new("library a; using b.c as d; extend enum d.t { x }; bool e;")),
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
                            value_type: ValueType::Enum,
                            extends: true,
                            values: vec![Value::Enum("x".to_string())],
                        },
                        Declaration {
                            identifier: make_identifier!["e"],
                            value_type: ValueType::Bool,
                            extends: false,
                            values: vec![],
                        },
                    ],
                },
            );
        }

        #[test]
        fn consumes_entire_input() {
            // Must parse entire input.
            assert_eq!(
                library(NomSpan::new("library a; using b.c as d; invalid input")),
                Err(nom::Err::Error(BindParserError::Type("invalid input".to_string())))
            );
        }

        #[test]
        fn whitespace() {
            // Handles whitespace.
            assert_eq!(
                library(NomSpan::new(
                    "\n\t library a;\t using b.c as d;\n extend enum d.t { x }; \t bool e;\n "
                ))
                .unwrap()
                .1,
                library(NomSpan::new("library a; using b.c as d; extend enum d.t { x }; bool e;"))
                    .unwrap()
                    .1,
            );
        }
    }
}
