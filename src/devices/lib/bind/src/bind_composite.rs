// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bind_program;
use crate::parser_common::{
    compound_identifier, many_until_eof, map_err, using_list, ws, BindParserError,
    CompoundIdentifier, Include, NomSpan,
};
use nom::{
    bytes::complete::tag,
    combinator::map,
    sequence::{delimited, preceded, tuple},
    IResult,
};
use std::convert::TryFrom;

#[derive(Debug, PartialEq)]
pub struct Ast<'a> {
    pub name: CompoundIdentifier,
    pub using: Vec<Include>,
    pub nodes: Vec<Vec<bind_program::Statement<'a>>>,
}

impl<'a> TryFrom<&'a str> for Ast<'a> {
    type Error = BindParserError;

    fn try_from(input: &'a str) -> Result<Self, Self::Error> {
        match composite(NomSpan::new(input)) {
            Ok((_, ast)) => Ok(ast),
            Err(nom::Err::Error(e)) => Err(e),
            Err(nom::Err::Failure(e)) => Err(e),
            Err(nom::Err::Incomplete(_)) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        }
    }
}

fn keyword_node(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(map_err(tag("node"), BindParserError::NodeKeyword))(input)
}

fn keyword_composite(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(map_err(tag("composite"), BindParserError::CompositeKeyword))(input)
}

fn composite_name(input: NomSpan) -> IResult<NomSpan, CompoundIdentifier, BindParserError> {
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    delimited(keyword_composite, ws(compound_identifier), terminator)(input)
}

fn node(input: NomSpan) -> IResult<NomSpan, Vec<bind_program::Statement>, BindParserError> {
    preceded(keyword_node, bind_program::statement_block)(input)
}

fn composite<'a>(input: NomSpan<'a>) -> IResult<NomSpan, Ast, BindParserError> {
    let nodes = |input: NomSpan<'a>| {
        let (input, nodes) = many_until_eof(ws(node))(input)?;
        if nodes.is_empty() {
            return Err(nom::Err::Error(BindParserError::NoNodes(input.to_string())));
        }
        Ok((input, nodes))
    };
    map(tuple((ws(composite_name), ws(using_list), nodes)), |(name, using, nodes)| Ast {
        name,
        using,
        nodes,
    })(input)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;
    use crate::parser_common::test::check_result;
    use crate::parser_common::Span;

    mod composite_name {
        use super::*;

        #[test]
        fn single_name() {
            check_result(composite_name(NomSpan::new("composite a;")), "", make_identifier!["a"]);
        }

        #[test]
        fn compound_name() {
            check_result(
                composite_name(NomSpan::new("composite a.b;")),
                "",
                make_identifier!["a", "b"],
            );
        }

        #[test]
        fn whitespace() {
            check_result(
                composite_name(NomSpan::new("composite \n\t a\n\t ;")),
                "",
                make_identifier!["a"],
            );
        }

        #[test]
        fn invalid() {
            // Must have a name.
            assert_eq!(
                composite_name(NomSpan::new("composite ;")),
                Err(nom::Err::Error(BindParserError::Identifier(";".to_string())))
            );

            // Must be terminated by ';'.
            assert_eq!(
                composite_name(NomSpan::new("composite a")),
                Err(nom::Err::Error(BindParserError::Semicolon("".to_string())))
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                composite_name(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::CompositeKeyword("".to_string())))
            );
        }
    }

    mod composites {
        use super::*;

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                composite(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::CompositeKeyword("".to_string())))
            );
        }

        #[test]
        fn one_node() {
            check_result(
                composite(NomSpan::new("composite a; node { true; }")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![],
                    nodes: vec![vec![bind_program::Statement::True {
                        span: Span { offset: 20, line: 1, fragment: "true;" },
                    }]],
                },
            );
        }

        #[test]
        fn two_nodes() {
            check_result(
                composite(NomSpan::new("composite a; node { true; } node { false; }")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![],
                    nodes: vec![
                        vec![bind_program::Statement::True {
                            span: Span { offset: 20, line: 1, fragment: "true;" },
                        }],
                        vec![bind_program::Statement::False {
                            span: Span { offset: 35, line: 1, fragment: "false;" },
                        }],
                    ],
                },
            );
        }

        #[test]
        fn using_list() {
            check_result(
                composite(NomSpan::new("composite a; using x.y as z; node { true; }")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![Include {
                        name: make_identifier!["x", "y"],
                        alias: Some("z".to_string()),
                    }],
                    nodes: vec![vec![bind_program::Statement::True {
                        span: Span { offset: 36, line: 1, fragment: "true;" },
                    }]],
                },
            );
        }

        #[test]
        fn no_nodes() {
            assert_eq!(
                composite(NomSpan::new("composite a; using x.y as z;")),
                Err(nom::Err::Error(BindParserError::NoNodes("".to_string())))
            );
            assert_eq!(
                composite(NomSpan::new("composite a;")),
                Err(nom::Err::Error(BindParserError::NoNodes("".to_string())))
            );
        }
    }
}
