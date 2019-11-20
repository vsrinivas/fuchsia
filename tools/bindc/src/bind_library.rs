// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::parser_common::{
    bool_literal, compound_identifier, identifier, many_until_eof, map_err, numeric_literal,
    string_literal, using_list, ws, BindParserError, CompoundIdentifier, Include,
};
use nom::{
    branch::alt,
    bytes::complete::{tag, take_until},
    combinator::{map, opt, value},
    multi::separated_nonempty_list,
    sequence::{delimited, separated_pair, terminated, tuple},
    IResult,
};
use std::str::FromStr;

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

#[derive(Debug, Copy, Clone, PartialEq)]
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

impl FromStr for Ast {
    type Err = BindParserError;

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

fn keyword_extend(input: &str) -> IResult<&str, &str, BindParserError> {
    ws(tag("extend"))(input)
}

fn keyword_uint(input: &str) -> IResult<&str, ValueType, BindParserError> {
    value(ValueType::Number, ws(map_err(tag("uint"), BindParserError::Type)))(input)
}

fn keyword_string(input: &str) -> IResult<&str, ValueType, BindParserError> {
    value(ValueType::Str, ws(map_err(tag("string"), BindParserError::Type)))(input)
}

fn keyword_bool(input: &str) -> IResult<&str, ValueType, BindParserError> {
    value(ValueType::Bool, ws(map_err(tag("bool"), BindParserError::Type)))(input)
}

fn keyword_enum(input: &str) -> IResult<&str, ValueType, BindParserError> {
    value(ValueType::Enum, ws(map_err(tag("enum"), BindParserError::Type)))(input)
}

fn value_list<'a, O, F>(f: F) -> impl Fn(&'a str) -> IResult<&'a str, Vec<O>, BindParserError>
where
    F: Fn(&'a str) -> IResult<&'a str, O, BindParserError>,
{
    move |input: &'a str| {
        let separator = || ws(map_err(tag(","), BindParserError::ListSeparator));
        let values = separated_nonempty_list(separator(), |s| f(s));

        // Lists may optionally be terminated by an additional trailing separator.
        let values = terminated(values, opt(separator()));

        // First consume all input until ';'. This simplifies the error handling since a semicolon
        // is mandatory, but a list of values is optional.
        let (input, vals_input) =
            map_err(terminated(take_until(";"), tag(";")), BindParserError::Semicolon)(input)?;

        if vals_input.is_empty() {
            return Ok((input, Vec::new()));
        }

        let list_start = map_err(tag("{"), BindParserError::ListStart);
        let list_end = map_err(tag("}"), BindParserError::ListEnd);
        let (_, result) = delimited(list_start, ws(values), list_end)(vals_input)?;

        Ok((input, result))
    }
}

fn number_value_list(input: &str) -> IResult<&str, Vec<Value>, BindParserError> {
    let token = map_err(tag("="), BindParserError::Assignment);
    let value = separated_pair(ws(identifier), token, ws(numeric_literal));
    value_list(map(value, |(ident, val)| Value::Number(ident, val)))(input)
}

fn string_value_list(input: &str) -> IResult<&str, Vec<Value>, BindParserError> {
    let token = map_err(tag("="), BindParserError::Assignment);
    let value = separated_pair(ws(identifier), token, ws(string_literal));
    value_list(map(value, |(ident, val)| Value::Str(ident, val)))(input)
}

fn bool_value_list(input: &str) -> IResult<&str, Vec<Value>, BindParserError> {
    let token = map_err(tag("="), BindParserError::Assignment);
    let value = separated_pair(ws(identifier), token, ws(bool_literal));
    value_list(map(value, |(ident, val)| Value::Bool(ident, val)))(input)
}

fn enum_value_list(input: &str) -> IResult<&str, Vec<Value>, BindParserError> {
    value_list(map(identifier, Value::Enum))(input)
}

fn declaration(input: &str) -> IResult<&str, Declaration, BindParserError> {
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

fn library_name(input: &str) -> IResult<&str, CompoundIdentifier, BindParserError> {
    let keyword = ws(map_err(tag("library"), BindParserError::LibraryKeyword));
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    delimited(keyword, ws(compound_identifier), terminator)(input)
}

fn library(input: &str) -> IResult<&str, Ast, BindParserError> {
    map(
        tuple((ws(library_name), ws(using_list), many_until_eof(ws(declaration)))),
        |(name, using, declarations)| Ast { name, using, declarations },
    )(input)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;

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
                Err(nom::Err::Error(BindParserError::NumericLiteral("\"string\"}".to_string())))
            );
            assert_eq!(
                number_value_list("{abc = true};"),
                Err(nom::Err::Error(BindParserError::NumericLiteral("true}".to_string())))
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
                Err(nom::Err::Error(BindParserError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                number_value_list("abc = 123};"),
                Err(nom::Err::Error(BindParserError::ListStart("abc = 123}".to_string())))
            );
            assert_eq!(
                number_value_list("{abc = 123;"),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list("{abc 123};"),
                Err(nom::Err::Error(BindParserError::Assignment("123}".to_string())))
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
                Err(nom::Err::Error(BindParserError::StringLiteral("123}".to_string())))
            );
            assert_eq!(
                string_value_list("{abc = true};"),
                Err(nom::Err::Error(BindParserError::StringLiteral("true}".to_string())))
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
                Err(nom::Err::Error(BindParserError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                string_value_list(r#"abc = "xyz"};"#),
                Err(nom::Err::Error(BindParserError::ListStart(r#"abc = "xyz"}"#.to_string())))
            );
            assert_eq!(
                string_value_list(r#"{abc = "xyz";"#),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list(r#"{abc "xyz"};"#),
                Err(nom::Err::Error(BindParserError::Assignment(r#""xyz"}"#.to_string())))
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
                Err(nom::Err::Error(BindParserError::BoolLiteral("123}".to_string())))
            );
            assert_eq!(
                bool_value_list("{abc = \"string\"};"),
                Err(nom::Err::Error(BindParserError::BoolLiteral("\"string\"}".to_string())))
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
                Err(nom::Err::Error(BindParserError::Identifier("}".to_string())))
            );
        }

        #[test]
        fn invalid_list() {
            // Must have list start and end braces.
            assert_eq!(
                bool_value_list("abc = true};"),
                Err(nom::Err::Error(BindParserError::ListStart("abc = true}".to_string())))
            );
            assert_eq!(
                bool_value_list("{abc = true;"),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );

            // Must have assignment operator.
            assert_eq!(
                number_value_list("{abc false};"),
                Err(nom::Err::Error(BindParserError::Assignment("false}".to_string())))
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
                Err(nom::Err::Error(BindParserError::Semicolon("{abc,}".to_string())))
            );

            // Must have list start and end braces.
            assert_eq!(
                enum_value_list("abc};"),
                Err(nom::Err::Error(BindParserError::ListStart("abc}".to_string())))
            );
            assert_eq!(
                enum_value_list("{abc;"),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );
        }

        #[test]
        fn empty_list() {
            // Does not match empty list.
            assert_eq!(
                enum_value_list("{};"),
                Err(nom::Err::Error(BindParserError::Identifier("}".to_string())))
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
                        value_type: ValueType::Number,
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
                        value_type: ValueType::Number,
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
                        value_type: ValueType::Str,
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
                        value_type: ValueType::Bool,
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
                        value_type: ValueType::Enum,
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
                        value_type: ValueType::Number,
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
                Err(nom::Err::Error(BindParserError::NumericLiteral("false }".to_string())))
            );
        }

        #[test]
        fn invalid() {
            // Must have a type, and an identifier.
            assert_eq!(
                declaration("bool { x = false };"),
                Err(nom::Err::Error(BindParserError::Identifier("{ x = false };".to_string())))
            );
            assert_eq!(
                declaration("test { x = false };"),
                Err(nom::Err::Error(BindParserError::Type("test { x = false };".to_string())))
            );

            // Must be terminated by ';'.
            assert_eq!(
                declaration("bool test { x = false }"),
                Err(nom::Err::Error(BindParserError::Semicolon("{ x = false }".to_string())))
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
                        value_type: ValueType::Number,
                        extends: false,
                        values: vec![Value::Number("x".to_string(), 1)],
                    }
                ))
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                declaration(""),
                Err(nom::Err::Error(BindParserError::Type("".to_string())))
            );
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
                Err(nom::Err::Error(BindParserError::Identifier(";".to_string())))
            );

            // Must be terminated by ';'.
            assert_eq!(
                library_name("library a"),
                Err(nom::Err::Error(BindParserError::Semicolon("".to_string())))
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                library_name(""),
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
                library(""),
                Err(nom::Err::Error(BindParserError::LibraryKeyword("".to_string())))
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
                            value_type: ValueType::Number,
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
                            value_type: ValueType::Enum,
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
                                value_type: ValueType::Enum,
                                extends: true,
                                values: vec![Value::Enum("x".to_string())],
                            },
                            Declaration {
                                identifier: make_identifier!["e"],
                                value_type: ValueType::Bool,
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
                Err(nom::Err::Error(BindParserError::Type("invalid input".to_string())))
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
