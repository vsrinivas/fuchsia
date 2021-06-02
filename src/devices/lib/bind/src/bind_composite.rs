// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bind_program::{statement_block, Statement, StatementBlock};
use crate::parser_common::{
    compound_identifier, many_until_eof, map_err, using_list, ws, BindParserError,
    CompoundIdentifier, Include, NomSpan,
};
use nom::{
    bytes::complete::tag,
    combinator::{map, opt},
    sequence::{delimited, tuple},
    IResult,
};
use std::convert::TryFrom;

#[derive(Debug, PartialEq)]
pub struct Ast<'a> {
    pub name: CompoundIdentifier,
    pub using: Vec<Include>,
    pub primary_node: StatementBlock<'a>,
    pub nodes: Vec<StatementBlock<'a>>,
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

fn keyword_composite(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(map_err(tag("composite"), BindParserError::CompositeKeyword))(input)
}

fn keyword_node(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(map_err(tag("node"), BindParserError::NodeKeyword))(input)
}

fn keyword_primary(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(map_err(tag("primary"), BindParserError::PrimaryKeyword))(input)
}

fn composite_name(input: NomSpan) -> IResult<NomSpan, CompoundIdentifier, BindParserError> {
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    delimited(keyword_composite, ws(compound_identifier), terminator)(input)
}

fn node(input: NomSpan) -> IResult<NomSpan, (bool, Vec<Statement>), BindParserError> {
    let (input, keywords) = tuple((opt(keyword_primary), keyword_node))(input)?;
    let is_primary = keywords.0.is_some();
    let (input, statements) = statement_block(input)?;
    return Ok((input, (is_primary, statements)));
}

fn composite<'a>(input: NomSpan<'a>) -> IResult<NomSpan, Ast, BindParserError> {
    let nodes = |input: NomSpan<'a>| -> IResult<
        NomSpan,
        (StatementBlock<'a>, Vec<StatementBlock<'a>>),
        BindParserError,
    > {
        let (input, nodes) = many_until_eof(ws(node))(input)?;
        if nodes.is_empty() {
            return Err(nom::Err::Error(BindParserError::NoNodes(input.to_string())));
        }
        let mut primary_node = None;
        let mut other_nodes = vec![];
        for (is_primary, node) in nodes {
            if is_primary {
                if primary_node.is_some() {
                    return Err(nom::Err::Error(BindParserError::OnePrimaryNode(
                        input.to_string(),
                    )));
                }
                primary_node = Some(node);
                continue;
            }
            other_nodes.push(node);
        }
        if let Some(primary_node) = primary_node {
            return Ok((input, (primary_node, other_nodes)));
        }
        return Err(nom::Err::Error(BindParserError::OnePrimaryNode(input.to_string())));
    };
    map(
        tuple((ws(composite_name), ws(using_list), nodes)),
        |(name, using, (primary_node, nodes))| Ast { name, using, primary_node, nodes },
    )(input)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bind_program::{Condition, ConditionOp};
    use crate::make_identifier;
    use crate::parser_common::test::check_result;
    use crate::parser_common::{Span, Value};

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
        fn one_primary_node() {
            check_result(
                composite(NomSpan::new("composite a; primary node { true; }")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![],
                    primary_node: vec![Statement::True {
                        span: Span { offset: 28, line: 1, fragment: "true;" },
                    }],
                    nodes: vec![],
                },
            );
        }

        #[test]
        fn one_primary_node_one_other() {
            check_result(
                composite(NomSpan::new("composite a; primary node { true; } node { false; }")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![],
                    primary_node: vec![Statement::True {
                        span: Span { offset: 28, line: 1, fragment: "true;" },
                    }],
                    nodes: vec![vec![Statement::False {
                        span: Span { offset: 43, line: 1, fragment: "false;" },
                    }]],
                },
            );
        }

        #[test]
        fn one_primary_node_two_others() {
            check_result(
                composite(NomSpan::new(
                    "composite a; primary node { true; } node { false; } node { x == 1; }",
                )),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![],
                    primary_node: vec![Statement::True {
                        span: Span { offset: 28, line: 1, fragment: "true;" },
                    }],
                    nodes: vec![
                        vec![Statement::False {
                            span: Span { offset: 43, line: 1, fragment: "false;" },
                        }],
                        vec![Statement::ConditionStatement {
                            span: Span { offset: 59, line: 1, fragment: "x == 1;" },
                            condition: Condition {
                                span: Span { offset: 59, line: 1, fragment: "x == 1" },
                                lhs: make_identifier!["x"],
                                op: ConditionOp::Equals,
                                rhs: Value::NumericLiteral(1),
                            },
                        }],
                    ],
                },
            );
        }

        #[test]
        fn using_list() {
            check_result(
                composite(NomSpan::new("composite a; using x.y as z; primary node { true; }")),
                "",
                Ast {
                    name: make_identifier!["a"],
                    using: vec![Include {
                        name: make_identifier!["x", "y"],
                        alias: Some("z".to_string()),
                    }],
                    primary_node: vec![Statement::True {
                        span: Span { offset: 44, line: 1, fragment: "true;" },
                    }],
                    nodes: vec![],
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

        #[test]
        fn not_one_primary_node() {
            assert_eq!(
                composite(NomSpan::new("composite a; node { true; }")),
                Err(nom::Err::Error(BindParserError::OnePrimaryNode("".to_string())))
            );
            assert_eq!(
                composite(NomSpan::new(
                    "composite a; primary node { true; } primary node { false; }"
                )),
                Err(nom::Err::Error(BindParserError::OnePrimaryNode("".to_string())))
            );
        }
    }
}
