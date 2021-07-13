// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// lazy_static is required for re_find_static.
use crate::errors::UserError;
use lazy_static::lazy_static;
use nom::{
    branch::alt,
    bytes::complete::{escaped, is_not, tag},
    character::complete::{
        char, digit1, hex_digit1, line_ending, multispace1, not_line_ending, one_of,
    },
    combinator::{map, map_res, opt, value},
    error::{ErrorKind, ParseError},
    multi::{many0, separated_nonempty_list},
    sequence::{delimited, preceded, tuple},
    IResult, Slice,
};
use nom_locate::LocatedSpan;
use regex::Regex;
use std::fmt;
use thiserror::Error;

pub type NomSpan<'a> = LocatedSpan<&'a str>;

#[derive(Debug, Clone, PartialEq)]
pub struct Span<'a> {
    pub offset: usize,
    pub line: u32,
    pub fragment: &'a str,
}

impl<'a> Span<'a> {
    pub fn new() -> Self {
        Span { offset: 0, line: 1, fragment: "" }
    }

    pub fn from_to(from: &NomSpan<'a>, to: &NomSpan) -> Self {
        Span {
            offset: from.location_offset(),
            line: from.location_line(),
            fragment: &from.fragment()[..to.location_offset() - from.location_offset()],
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CompoundIdentifier {
    pub namespace: Vec<String>,
    pub name: String,
}

impl fmt::Display for CompoundIdentifier {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.namespace.is_empty() {
            write!(f, "{}", self.name)
        } else {
            write!(f, "{}.{}", self.namespace.join("."), self.name)
        }
    }
}

impl CompoundIdentifier {
    pub fn nest(&self, name: String) -> CompoundIdentifier {
        let mut namespace = self.namespace.clone();
        namespace.push(self.name.clone());
        CompoundIdentifier { namespace, name }
    }

    pub fn parent(&self) -> Option<CompoundIdentifier> {
        let mut namespace = self.namespace.clone();
        let name = namespace.pop()?;
        Some(CompoundIdentifier { namespace, name })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Include {
    pub name: CompoundIdentifier,
    pub alias: Option<String>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    NumericLiteral(u64),
    StringLiteral(String),
    BoolLiteral(bool),
    Identifier(CompoundIdentifier),
}

#[derive(Debug, Error, Clone, PartialEq)]
pub enum BindParserError {
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
    IfBlockStart(String),
    IfBlockEnd(String),
    IfKeyword(String),
    ElseKeyword(String),
    ConditionOp(String),
    ConditionValue(String),
    AcceptKeyword(String),
    TrueKeyword(String),
    FalseKeyword(String),
    NoStatements(String),
    NoNodes(String),
    Eof(String),
    CompositeKeyword(String),
    NodeKeyword(String),
    PrimaryKeyword(String),
    OnePrimaryNode(String),
    UnterminatedComment,
    Unknown(String, ErrorKind),
}

impl fmt::Display for BindParserError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

impl ParseError<NomSpan<'_>> for BindParserError {
    fn from_error_kind(input: NomSpan, kind: ErrorKind) -> Self {
        BindParserError::Unknown(input.fragment().to_string(), kind)
    }

    fn append(_input: NomSpan, _kind: ErrorKind, e: Self) -> Self {
        e
    }
}

pub fn string_literal(input: NomSpan) -> IResult<NomSpan, String, BindParserError> {
    let escapable = escaped(is_not(r#"\""#), '\\', one_of(r#"\""#));
    let literal = delimited(char('"'), escapable, char('"'));
    map_err(map(literal, |s: NomSpan| s.fragment().to_string()), BindParserError::StringLiteral)(
        input,
    )
}

pub fn numeric_literal(input: NomSpan) -> IResult<NomSpan, u64, BindParserError> {
    let base10 = map_res(digit1, |s: NomSpan| u64::from_str_radix(s.fragment(), 10));
    let base16 = map_res(preceded(tag("0x"), hex_digit1), |s: NomSpan| {
        u64::from_str_radix(s.fragment(), 16)
    });
    // Note: When the base16 parser fails but input starts with '0x' this will succeed and return 0.
    map_err(alt((base16, base10)), BindParserError::NumericLiteral)(input)
}

pub fn bool_literal(input: NomSpan) -> IResult<NomSpan, bool, BindParserError> {
    let true_ = value(true, tag("true"));
    let false_ = value(false, tag("false"));
    map_err(alt((true_, false_)), BindParserError::BoolLiteral)(input)
}

pub fn identifier(input: NomSpan) -> IResult<NomSpan, String, BindParserError> {
    lazy_static! {
        static ref RE: Regex = Regex::new(r"^[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?").unwrap();
    }
    if let Some(mat) = RE.find(input.fragment()) {
        Ok((input.slice(mat.end()..), input.slice(mat.start()..mat.end()).fragment().to_string()))
    } else {
        Err(nom::Err::Error(BindParserError::Identifier(input.fragment().to_string())))
    }
}

pub fn compound_identifier(
    input: NomSpan,
) -> IResult<NomSpan, CompoundIdentifier, BindParserError> {
    let (input, mut segments) = separated_nonempty_list(tag("."), identifier)(input)?;
    // Segments must be nonempty, so it's safe to pop off the name.
    let name = segments.pop().unwrap();
    Ok((input, CompoundIdentifier { namespace: segments, name }))
}

pub fn condition_value(input: NomSpan) -> IResult<NomSpan, Value, BindParserError> {
    let string = map(ws(string_literal), Value::StringLiteral);
    let number = map(ws(numeric_literal), Value::NumericLiteral);
    let boolean = map(ws(bool_literal), Value::BoolLiteral);
    let identifer = map(ws(compound_identifier), Value::Identifier);

    alt((string, number, boolean, identifer))(input)
        .or(Err(nom::Err::Error(BindParserError::ConditionValue(input.to_string()))))
}

pub fn using(input: NomSpan) -> IResult<NomSpan, Include, BindParserError> {
    let as_keyword = ws(map_err(tag("as"), BindParserError::AsKeyword));
    let (input, name) = ws(compound_identifier)(input)?;
    let (input, alias) = opt(preceded(as_keyword, ws(identifier)))(input)?;
    Ok((input, Include { name, alias }))
}

pub fn using_list(input: NomSpan) -> IResult<NomSpan, Vec<Include>, BindParserError> {
    let using_keyword = ws(map_err(tag("using"), BindParserError::UsingKeyword));
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    let using = delimited(using_keyword, ws(using), terminator);
    many0(ws(using))(input)
}

/// Applies the parser `f` until reaching the end of the input. `f` must always make progress (i.e.
/// consume input) and many_until_eof will panic if it doesn't, to prevent infinite loops. Returns
/// the results of `f` in a Vec.
pub fn many_until_eof<'a, O, F>(
    f: F,
) -> impl Fn(NomSpan<'a>) -> IResult<NomSpan<'a>, Vec<O>, BindParserError>
where
    F: Fn(NomSpan<'a>) -> IResult<NomSpan<'a>, O, BindParserError>,
{
    move |mut input: NomSpan<'a>| {
        let mut result = vec![];
        loop {
            // Ignore trailing whitespace at the end of the file.
            if skip_ws(input)?.fragment().len() == 0 {
                return Ok((skip_ws(input)?, result));
            }

            let (next_input, res) = f(input)?;
            if input.fragment().len() == next_input.fragment().len() {
                panic!("many_until_eof called on an optional parser. This will result in an infinite loop");
            }
            input = next_input;
            result.push(res);
        }
    }
}

/// Wraps a parser |f| and discards zero or more whitespace characters or comments before it.
/// Doesn't discard whitespace after the parser, since this would make it difficult to ensure that
/// the AST spans contain no trailing whitespace.
pub fn ws<'a, O, F>(f: F) -> impl Fn(NomSpan<'a>) -> IResult<NomSpan<'a>, O, BindParserError>
where
    F: Fn(NomSpan<'a>) -> IResult<NomSpan<'a>, O, BindParserError>,
{
    preceded(comment_or_whitespace, f)
}

pub fn skip_ws(input: NomSpan) -> Result<NomSpan, nom::Err<BindParserError>> {
    let (input, _) = comment_or_whitespace(input)?;
    Ok(input)
}

fn comment_or_whitespace(input: NomSpan) -> IResult<NomSpan, (), BindParserError> {
    let multispace = value((), multispace1);
    value((), many0(alt((multispace, multiline_comment, singleline_comment))))(input)
}

/// Parser that matches a multiline comment, e.g. "/* comment */". Comments may be nested.
fn multiline_comment(input: NomSpan) -> IResult<NomSpan, (), BindParserError> {
    let (input, _) = tag("/*")(input)?;
    let mut iter = input.fragment().char_indices().peekable();
    let mut stack = 1;
    while let (Some((_, first)), Some((_, second))) = (iter.next(), iter.peek()) {
        if first == '/' && *second == '*' {
            stack += 1;
            iter.next();
        } else if first == '*' && *second == '/' {
            stack -= 1;
            iter.next();
            if stack == 0 {
                break;
            }
        }
    }
    if stack != 0 {
        return Err(nom::Err::Failure(BindParserError::UnterminatedComment));
    }

    let consumed = if let Some((index, _)) = iter.peek() { *index } else { input.fragment().len() };
    Ok((input.slice(consumed..), ()))
}

/// Parser that matches a single line comment, e.g. "// comment\n".
fn singleline_comment(input: NomSpan) -> IResult<NomSpan, (), BindParserError> {
    value((), tuple((tag("//"), not_line_ending, line_ending)))(input)
}

// Wraps a parser and replaces its error.
pub fn map_err<'a, O, P, G>(
    parser: P,
    f: G,
) -> impl Fn(NomSpan<'a>) -> IResult<NomSpan<'a>, O, BindParserError>
where
    P: Fn(NomSpan<'a>) -> IResult<NomSpan<'a>, O, (NomSpan<'a>, ErrorKind)>,
    G: Fn(String) -> BindParserError,
{
    move |input: NomSpan| {
        parser(input).map_err(|e| match e {
            nom::Err::Error((i, _)) => nom::Err::Error(f(i.to_string())),
            nom::Err::Failure((i, _)) => nom::Err::Failure(f(i.to_string())),
            nom::Err::Incomplete(_) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        })
    }
}

#[macro_export]
macro_rules! make_identifier{
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

#[cfg(test)]
pub mod test {
    use super::*;

    pub fn check_result<O: PartialEq + fmt::Debug>(
        result: IResult<NomSpan, O, BindParserError>,
        expected_input_fragment: &str,
        expected_output: O,
    ) {
        match result {
            Ok((input, output)) => {
                assert_eq!(input.fragment(), &expected_input_fragment);
                assert_eq!(output, expected_output);
            }
            Err(e) => {
                panic!("{:#?}", e);
            }
        }
    }

    mod string_literal {
        use super::*;

        #[test]
        fn basic() {
            // Matches a string literal, leaves correct tail.
            check_result(
                string_literal(NomSpan::new(r#""abc 123"xyz"#)),
                "xyz",
                "abc 123".to_string(),
            );
        }

        #[test]
        fn match_once() {
            check_result(
                string_literal(NomSpan::new(r#""abc""123""#)),
                r#""123""#,
                "abc".to_string(),
            );
        }

        #[test]
        fn escaped() {
            check_result(
                string_literal(NomSpan::new(r#""abc \"esc\" xyz""#)),
                "",
                r#"abc \"esc\" xyz"#.to_string(),
            );
        }

        #[test]
        fn requires_quotations() {
            assert_eq!(
                string_literal(NomSpan::new(r#"abc"#)),
                Err(nom::Err::Error(BindParserError::StringLiteral("abc".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                string_literal(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::StringLiteral("".to_string())))
            );
        }
    }

    mod numeric_literals {
        use super::*;

        #[test]
        fn decimal() {
            check_result(numeric_literal(NomSpan::new("123")), "", 123);
        }

        #[test]
        fn hex() {
            check_result(numeric_literal(NomSpan::new("0x123")), "", 0x123);
            check_result(numeric_literal(NomSpan::new("0xabcdef")), "", 0xabcdef);
            check_result(numeric_literal(NomSpan::new("0xABCDEF")), "", 0xabcdef);
            check_result(numeric_literal(NomSpan::new("0x123abc")), "", 0x123abc);

            // Does not match hex without '0x' prefix.
            assert_eq!(
                numeric_literal(NomSpan::new("abc123")),
                Err(nom::Err::Error(BindParserError::NumericLiteral("abc123".to_string())))
            );
            check_result(numeric_literal(NomSpan::new("123abc")), "abc", 123);
        }

        #[test]
        fn non_numbers() {
            assert_eq!(
                numeric_literal(NomSpan::new("xyz")),
                Err(nom::Err::Error(BindParserError::NumericLiteral("xyz".to_string())))
            );

            // Does not match negative numbers (for now).
            assert_eq!(
                numeric_literal(NomSpan::new("-1")),
                Err(nom::Err::Error(BindParserError::NumericLiteral("-1".to_string())))
            );
        }

        #[test]
        fn overflow() {
            // Does not match numbers larger than u64.
            check_result(
                numeric_literal(NomSpan::new("18446744073709551615")),
                "",
                18446744073709551615,
            );
            check_result(
                numeric_literal(NomSpan::new("0xffffffffffffffff")),
                "",
                18446744073709551615,
            );
            assert_eq!(
                numeric_literal(NomSpan::new("18446744073709551616")),
                Err(nom::Err::Error(BindParserError::NumericLiteral(
                    "18446744073709551616".to_string()
                )))
            );
            // Note: This is matching '0' from '0x' but failing to parse the entire string.
            check_result(
                numeric_literal(NomSpan::new("0x10000000000000000")),
                "x10000000000000000",
                0,
            );
        }

        #[test]
        fn empty() {
            // Does not match an empty string.
            assert_eq!(
                numeric_literal(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::NumericLiteral("".to_string())))
            );
        }
    }

    mod bool_literals {
        use super::*;

        #[test]
        fn basic() {
            check_result(bool_literal(NomSpan::new("true")), "", true);
            check_result(bool_literal(NomSpan::new("false")), "", false);
        }

        #[test]
        fn non_bools() {
            // Does not match anything else.
            assert_eq!(
                bool_literal(NomSpan::new("tralse")),
                Err(nom::Err::Error(BindParserError::BoolLiteral("tralse".to_string())))
            );
        }

        #[test]
        fn empty() {
            // Does not match an empty string.
            assert_eq!(
                bool_literal(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::BoolLiteral("".to_string())))
            );
        }
    }

    mod identifiers {
        use super::*;

        #[test]
        fn basic() {
            // Matches identifiers with lowercase, uppercase, digits, and underscores.
            check_result(identifier(NomSpan::new("abc_123_ABC")), "", "abc_123_ABC".to_string());

            // Match is terminated by whitespace or punctuation.
            check_result(identifier(NomSpan::new("abc_123_ABC ")), " ", "abc_123_ABC".to_string());
            check_result(identifier(NomSpan::new("abc_123_ABC;")), ";", "abc_123_ABC".to_string());
        }

        #[test]
        fn invalid() {
            // Does not match an identifier beginning or ending with '_'.
            assert_eq!(
                identifier(NomSpan::new("_abc")),
                Err(nom::Err::Error(BindParserError::Identifier("_abc".to_string())))
            );

            // Note: Matches up until the '_' but fails to parse the entire string.
            check_result(identifier(NomSpan::new("abc_")), "_", "abc".to_string());
        }

        #[test]
        fn empty() {
            // Does not match an empty string.
            assert_eq!(
                identifier(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::Identifier("".to_string())))
            );
        }
    }

    mod compound_identifiers {
        use super::*;

        #[test]
        fn single_identifier() {
            // Matches single identifier.
            check_result(
                compound_identifier(NomSpan::new("abc_123_ABC")),
                "",
                make_identifier!["abc_123_ABC"],
            );
        }

        #[test]
        fn multiple_identifiers() {
            // Matches compound identifiers.
            check_result(
                compound_identifier(NomSpan::new("abc.def")),
                "",
                make_identifier!["abc", "def"],
            );
            check_result(
                compound_identifier(NomSpan::new("abc.def.ghi")),
                "",
                make_identifier!["abc", "def", "ghi"],
            );
        }

        #[test]
        fn empty() {
            // Does not match empty identifiers.
            assert_eq!(
                compound_identifier(NomSpan::new(".")),
                Err(nom::Err::Error(BindParserError::Identifier(".".to_string())))
            );
            assert_eq!(
                compound_identifier(NomSpan::new(".abc")),
                Err(nom::Err::Error(BindParserError::Identifier(".abc".to_string())))
            );
            check_result(
                compound_identifier(NomSpan::new("abc..def")),
                "..def",
                make_identifier!["abc"],
            );
            check_result(compound_identifier(NomSpan::new("abc.")), ".", make_identifier!["abc"]);

            // Does not match an empty string.
            assert_eq!(
                compound_identifier(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::Identifier("".to_string())))
            );
        }
    }

    mod condition_values {
        use super::*;

        #[test]
        fn string() {
            check_result(
                condition_value(NomSpan::new(r#""abc""#)),
                "",
                Value::StringLiteral("abc".to_string()),
            );
        }

        #[test]
        fn bool() {
            check_result(condition_value(NomSpan::new("true")), "", Value::BoolLiteral(true));
        }

        #[test]
        fn number() {
            check_result(condition_value(NomSpan::new("123")), "", Value::NumericLiteral(123));
        }

        #[test]
        fn identifier() {
            check_result(
                condition_value(NomSpan::new("abc")),
                "",
                Value::Identifier(make_identifier!["abc"]),
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                condition_value(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::ConditionValue("".to_string())))
            );
        }
    }

    mod using_lists {
        use super::*;

        #[test]
        fn one_include() {
            check_result(
                using_list(NomSpan::new("using test;")),
                "",
                vec![Include { name: make_identifier!["test"], alias: None }],
            );
        }

        #[test]
        fn multiple_includes() {
            check_result(
                using_list(NomSpan::new("using abc;using def;")),
                "",
                vec![
                    Include { name: make_identifier!["abc"], alias: None },
                    Include { name: make_identifier!["def"], alias: None },
                ],
            );
            check_result(
                using_list(NomSpan::new("using abc;using def;using ghi;")),
                "",
                vec![
                    Include { name: make_identifier!["abc"], alias: None },
                    Include { name: make_identifier!["def"], alias: None },
                    Include { name: make_identifier!["ghi"], alias: None },
                ],
            );
        }

        #[test]
        fn compound_identifiers() {
            check_result(
                using_list(NomSpan::new("using abc.def;")),
                "",
                vec![Include { name: make_identifier!["abc", "def"], alias: None }],
            );
        }

        #[test]
        fn aliases() {
            check_result(
                using_list(NomSpan::new("using abc.def as one;using ghi as two;")),
                "",
                vec![
                    Include {
                        name: make_identifier!["abc", "def"],
                        alias: Some("one".to_string()),
                    },
                    Include { name: make_identifier!["ghi"], alias: Some("two".to_string()) },
                ],
            );
        }

        #[test]
        fn whitespace() {
            check_result(
                using_list(NomSpan::new(" using   abc\t as  one  ;\n using def ; ")),
                " ",
                vec![
                    Include { name: make_identifier!["abc"], alias: Some("one".to_string()) },
                    Include { name: make_identifier!["def"], alias: None },
                ],
            );
            check_result(
                using_list(NomSpan::new("usingabc;")),
                "",
                vec![Include { name: make_identifier!["abc"], alias: None }],
            );
        }

        #[test]
        fn invalid() {
            // Must be followed by ';'.
            check_result(using_list(NomSpan::new("using abc")), "using abc", vec![]);
        }

        #[test]
        fn empty() {
            check_result(using_list(NomSpan::new("")), "", vec![]);
        }
    }

    mod whitespace {
        use super::*;

        #[test]
        fn multiline_comments() {
            check_result(multiline_comment(NomSpan::new("/*one*/")), "", ());
            check_result(multiline_comment(NomSpan::new("/*one*/two")), "two", ());
            check_result(
                multiline_comment(NomSpan::new("/*one/*two*/three/*four*/five*/six")),
                "six",
                (),
            );
            check_result(multiline_comment(NomSpan::new("/*/*one*/*/two")), "two", ());
            assert_eq!(
                multiline_comment(NomSpan::new("/*one")),
                Err(nom::Err::Failure(BindParserError::UnterminatedComment))
            );

            // Span is updated correctly.
            let (input, _) = multiline_comment(NomSpan::new("/*\n/*one\n*/*/two")).unwrap();
            assert_eq!(input.location_offset(), 13);
            assert_eq!(input.location_line(), 3);
            assert_eq!(input.fragment(), &"two");

            let (input, _) = multiline_comment(NomSpan::new("/*\n/*one\n*/*/")).unwrap();
            assert_eq!(input.location_offset(), 13);
            assert_eq!(input.location_line(), 3);
            assert_eq!(input.fragment(), &"");
        }

        #[test]
        fn singleline_comments() {
            check_result(singleline_comment(NomSpan::new("//one\ntwo")), "two", ());
            check_result(singleline_comment(NomSpan::new("//one\r\ntwo")), "two", ());
        }

        #[test]
        fn whitespace() {
            let test = || map(tag("test"), |s: NomSpan| s.fragment().to_string());
            check_result(ws(test())(NomSpan::new("test")), "", "test".to_string());

            check_result(ws(test())(NomSpan::new(" \n\t\r\ntest")), "", "test".to_string());
            check_result(
                ws(test())(NomSpan::new("test \n\t\r\n")),
                " \n\t\r\n",
                "test".to_string(),
            );

            check_result(
                ws(test())(NomSpan::new(" // test \n test // test \n ")),
                " // test \n ",
                "test".to_string(),
            );

            check_result(
                ws(test())(NomSpan::new(" /* test */ test /* test */ ")),
                " /* test */ ",
                "test".to_string(),
            );
        }

        #[test]
        fn skip_whitespace() {
            let result = skip_ws(NomSpan::new("test")).unwrap();
            assert_eq!(result.location_offset(), 0);
            assert_eq!(result.location_line(), 1);
            assert_eq!(result.fragment(), &"test");

            let result = skip_ws(NomSpan::new(" \n\t\r\ntest \n\t\r\n")).unwrap();
            assert_eq!(result.location_offset(), 5);
            assert_eq!(result.location_line(), 3);
            assert_eq!(result.fragment(), &"test \n\t\r\n");

            let result = skip_ws(NomSpan::new(" // test \n test // test \n ")).unwrap();
            assert_eq!(result.location_offset(), 11);
            assert_eq!(result.location_line(), 2);
            assert_eq!(result.fragment(), &"test // test \n ");

            let result = skip_ws(NomSpan::new(" /* test */ test /* test */ ")).unwrap();
            assert_eq!(result.location_offset(), 12);
            assert_eq!(result.location_line(), 1);
            assert_eq!(result.fragment(), &"test /* test */ ");
        }
    }
}
