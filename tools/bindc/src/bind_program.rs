// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::parser_common::{
    bool_literal, compound_identifier, many_until_eof, map_err, numeric_literal, string_literal,
    using_list, ws, BindParserError, CompoundIdentifier, Include,
};
use nom::{
    branch::alt,
    bytes::complete::tag,
    combinator::{map, opt, value},
    multi::{many1, separated_nonempty_list},
    sequence::{delimited, preceded, terminated, tuple},
    IResult,
};
use std::str::FromStr;

#[derive(Debug, Clone, PartialEq)]
pub struct Ast {
    pub using: Vec<Include>,
    pub statements: Vec<Statement>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ConditionOp {
    Equals,
    NotEquals,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    NumericLiteral(u64),
    StringLiteral(String),
    BoolLiteral(bool),
    Identifier(CompoundIdentifier),
}

#[derive(Debug, Clone, PartialEq)]
pub struct Condition {
    pub lhs: CompoundIdentifier,
    pub op: ConditionOp,
    pub rhs: Value,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Statement {
    ConditionStatement(Condition),
    Accept { identifier: CompoundIdentifier, values: Vec<Value> },
    If { blocks: Vec<(Condition, Vec<Statement>)>, else_block: Vec<Statement> },
    Abort,
}

// TODO(fxb/35146): Improve error reporting here.
impl FromStr for Ast {
    type Err = BindParserError;

    fn from_str(input: &str) -> Result<Self, Self::Err> {
        match program(input) {
            Ok((_, ast)) => Ok(ast),
            Err(nom::Err::Error(e)) => Err(e),
            Err(nom::Err::Failure(e)) => Err(e),
            Err(nom::Err::Incomplete(_)) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        }
    }
}

fn condition_op(input: &str) -> IResult<&str, ConditionOp, BindParserError> {
    let equals = value(ConditionOp::Equals, tag("=="));
    let not_equals = value(ConditionOp::NotEquals, tag("!="));
    map_err(alt((equals, not_equals)), BindParserError::ConditionOp)(input)
}

fn condition_value(input: &str) -> IResult<&str, Value, BindParserError> {
    let string = map(ws(string_literal), Value::StringLiteral);
    let number = map(ws(numeric_literal), Value::NumericLiteral);
    let boolean = map(ws(bool_literal), Value::BoolLiteral);
    let identifer = map(ws(compound_identifier), Value::Identifier);

    alt((string, number, boolean, identifer))(input)
        .or(Err(nom::Err::Error(BindParserError::ConditionValue(input.to_string()))))
}

fn condition(input: &str) -> IResult<&str, Condition, BindParserError> {
    let (input, lhs) = ws(compound_identifier)(input)?;
    let (input, op) = condition_op(input)?;
    let (input, rhs) = condition_value(input)?;
    Ok((input, Condition { lhs, op, rhs }))
}

fn condition_statement(input: &str) -> IResult<&str, Statement, BindParserError> {
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    map(terminated(condition, terminator), Statement::ConditionStatement)(input)
}

fn keyword_if(input: &str) -> IResult<&str, &str, BindParserError> {
    ws(map_err(tag("if"), BindParserError::IfKeyword))(input)
}

fn keyword_else(input: &str) -> IResult<&str, &str, BindParserError> {
    ws(map_err(tag("else"), BindParserError::ElseKeyword))(input)
}

fn if_statement(input: &str) -> IResult<&str, Statement, BindParserError> {
    let if_block = tuple((preceded(keyword_if, condition), statement_block));
    let if_blocks = separated_nonempty_list(keyword_else, if_block);

    let else_block = preceded(keyword_else, statement_block);

    let (input, blocks) = if_blocks(input)?;
    let (input, else_block) = else_block(input)?;
    Ok((input, Statement::If { blocks, else_block }))
}

fn statement_block(input: &str) -> IResult<&str, Vec<Statement>, BindParserError> {
    let block_start = map_err(tag("{"), BindParserError::IfBlockStart);
    let block_end = map_err(tag("}"), BindParserError::IfBlockEnd);
    delimited(block_start, many1(ws(statement)), block_end)(input)
}

fn keyword_accept(input: &str) -> IResult<&str, &str, BindParserError> {
    ws(map_err(tag("accept"), BindParserError::AcceptKeyword))(input)
}

fn accept(input: &str) -> IResult<&str, Statement, BindParserError> {
    let list_start = map_err(tag("{"), BindParserError::ListStart);
    let list_end = map_err(tag("}"), BindParserError::ListEnd);
    let separator = || ws(map_err(tag(","), BindParserError::ListSeparator));

    let values = separated_nonempty_list(separator(), ws(condition_value));
    // Lists may optionally be terminated by an additional trailing separator.
    let values = terminated(values, opt(separator()));
    let value_list = delimited(list_start, values, list_end);

    map(
        preceded(keyword_accept, tuple((ws(compound_identifier), value_list))),
        |(identifier, values)| Statement::Accept { identifier, values },
    )(input)
}

fn abort(input: &str) -> IResult<&str, Statement, BindParserError> {
    let keyword_abort = ws(map_err(tag("abort"), BindParserError::AbortKeyword));
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    let (input, _) = terminated(keyword_abort, terminator)(input)?;
    Ok((input, Statement::Abort))
}

fn statement(input: &str) -> IResult<&str, Statement, BindParserError> {
    alt((condition_statement, if_statement, accept, abort))(input)
}

fn program(input: &str) -> IResult<&str, Ast, BindParserError> {
    let (input, using) = ws(using_list)(input)?;
    let (input, statements) = many_until_eof(ws(statement))(input)?;
    if statements.is_empty() {
        return Err(nom::Err::Error(BindParserError::NoStatements(input.to_string())));
    }
    Ok(("", Ast { using, statements }))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;

    mod condition_ops {
        use super::*;

        #[test]
        fn equality() {
            assert_eq!(condition_op("=="), Ok(("", ConditionOp::Equals)));
        }

        #[test]
        fn inequality() {
            assert_eq!(condition_op("!="), Ok(("", ConditionOp::NotEquals)));
        }

        #[test]
        fn invalid() {
            assert_eq!(
                condition_op(">="),
                Err(nom::Err::Error(BindParserError::ConditionOp(">=".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                condition_op(""),
                Err(nom::Err::Error(BindParserError::ConditionOp("".to_string())))
            );
        }
    }

    mod condition_values {
        use super::*;

        #[test]
        fn string() {
            assert_eq!(
                condition_value(r#""abc""#),
                Ok(("", Value::StringLiteral("abc".to_string())))
            );
        }

        #[test]
        fn bool() {
            assert_eq!(condition_value("true"), Ok(("", Value::BoolLiteral(true))));
        }

        #[test]
        fn number() {
            assert_eq!(condition_value("123"), Ok(("", Value::NumericLiteral(123))));
        }

        #[test]
        fn identifier() {
            assert_eq!(
                condition_value("abc"),
                Ok(("", Value::Identifier(make_identifier!["abc"])))
            );
        }

        #[test]
        fn empty() {
            // Does not match empty string.
            assert_eq!(
                condition_value(""),
                Err(nom::Err::Error(BindParserError::ConditionValue("".to_string())))
            );
        }
    }

    mod conditions {
        use super::*;

        #[test]
        fn equality_condition() {
            assert_eq!(
                condition("abc == true"),
                Ok((
                    "",
                    Condition {
                        lhs: make_identifier!["abc"],
                        op: ConditionOp::Equals,
                        rhs: Value::BoolLiteral(true),
                    }
                ))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                condition(""),
                Err(nom::Err::Error(BindParserError::Identifier("".to_string())))
            );
        }
    }

    mod if_statements {
        use super::*;

        #[test]
        fn simple() {
            assert_eq!(
                if_statement("if a == b { c == 1; } else { d == 2; }"),
                Ok((
                    "",
                    Statement::If {
                        blocks: vec![(
                            Condition {
                                lhs: make_identifier!["a"],
                                op: ConditionOp::Equals,
                                rhs: Value::Identifier(make_identifier!["b"]),
                            },
                            vec![Statement::ConditionStatement(Condition {
                                lhs: make_identifier!["c"],
                                op: ConditionOp::Equals,
                                rhs: Value::NumericLiteral(1),
                            })]
                        )],
                        else_block: vec![Statement::ConditionStatement(Condition {
                            lhs: make_identifier!["d"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(2),
                        })],
                    }
                ))
            );
        }

        #[test]
        fn else_if() {
            assert_eq!(
                if_statement("if a == b { c == 1; } else if e == 3 { d == 2; } else { f != 4; }"),
                Ok((
                    "",
                    Statement::If {
                        blocks: vec![
                            (
                                Condition {
                                    lhs: make_identifier!["a"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::Identifier(make_identifier!["b"]),
                                },
                                vec![Statement::ConditionStatement(Condition {
                                    lhs: make_identifier!["c"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(1),
                                })]
                            ),
                            (
                                Condition {
                                    lhs: make_identifier!["e"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(3),
                                },
                                vec![Statement::ConditionStatement(Condition {
                                    lhs: make_identifier!["d"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(2),
                                })]
                            ),
                        ],
                        else_block: vec![Statement::ConditionStatement(Condition {
                            lhs: make_identifier!["f"],
                            op: ConditionOp::NotEquals,
                            rhs: Value::NumericLiteral(4),
                        })],
                    }
                ))
            );
        }

        #[test]
        fn nested() {
            assert_eq!(
                if_statement(
                    "if a == 1 { if b == 2 { c != 3; } else { c == 3; } } else { d == 2; }"
                ),
                Ok((
                    "",
                    Statement::If {
                        blocks: vec![(
                            Condition {
                                lhs: make_identifier!["a"],
                                op: ConditionOp::Equals,
                                rhs: Value::NumericLiteral(1),
                            },
                            vec![Statement::If {
                                blocks: vec![(
                                    Condition {
                                        lhs: make_identifier!["b"],
                                        op: ConditionOp::Equals,
                                        rhs: Value::NumericLiteral(2),
                                    },
                                    vec![Statement::ConditionStatement(Condition {
                                        lhs: make_identifier!["c"],
                                        op: ConditionOp::NotEquals,
                                        rhs: Value::NumericLiteral(3),
                                    })],
                                )],
                                else_block: vec![Statement::ConditionStatement(Condition {
                                    lhs: make_identifier!["c"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(3),
                                })],
                            }]
                        )],
                        else_block: vec![Statement::ConditionStatement(Condition {
                            lhs: make_identifier!["d"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(2),
                        })],
                    }
                ))
            );
        }

        #[test]
        fn invalid() {
            // Must have 'if' keyword.
            assert_eq!(
                if_statement("a == b { c == 1; }"),
                Err(nom::Err::Error(BindParserError::IfKeyword("a == b { c == 1; }".to_string())))
            );

            // Must have condition.
            assert_eq!(
                if_statement("if { c == 1; }"),
                Err(nom::Err::Error(BindParserError::Identifier("{ c == 1; }".to_string())))
            );

            // Must have else block.
            assert_eq!(
                if_statement("if a == b { c == 1; }"),
                Err(nom::Err::Error(BindParserError::ElseKeyword("".to_string())))
            );
            assert_eq!(
                if_statement("if a == b { c == 1; } else if e == 3 { d == 2; }"),
                Err(nom::Err::Error(BindParserError::ElseKeyword("".to_string())))
            );

            // Must delimit blocks with {}s.
            assert_eq!(
                if_statement("if a == b c == 1; }"),
                Err(nom::Err::Error(BindParserError::IfBlockStart("c == 1; }".to_string())))
            );
            assert_eq!(
                if_statement("if a == b { c == 1;"),
                Err(nom::Err::Error(BindParserError::IfBlockEnd("".to_string())))
            );

            // Blocks must not be empty.
            // TODO(fxb/35146): Improve this error message, it currently reports a failure to parse
            // an accept statement due to the way the combinator works for the statement parser.
            assert!(if_statement("if a == b { } else { c == 1; }").is_err());
            assert!(if_statement("if a == b { c == 1; } else { }").is_err());
        }

        #[test]
        fn empty() {
            assert_eq!(
                if_statement(""),
                Err(nom::Err::Error(BindParserError::IfKeyword("".to_string())))
            );
        }
    }

    mod accepts {
        use super::*;

        #[test]
        fn simple() {
            assert_eq!(
                accept("accept a { 1 }"),
                Ok((
                    "",
                    Statement::Accept {
                        identifier: make_identifier!["a"],
                        values: vec![Value::NumericLiteral(1)],
                    }
                ))
            );
        }

        #[test]
        fn multiple_values() {
            assert_eq!(
                accept("accept a { 1, 2 }"),
                Ok((
                    "",
                    Statement::Accept {
                        identifier: make_identifier!["a"],
                        values: vec![Value::NumericLiteral(1), Value::NumericLiteral(2),],
                    }
                ))
            );
        }

        #[test]
        fn trailing_comma() {
            assert_eq!(
                accept("accept a { 1, 2, }"),
                Ok((
                    "",
                    Statement::Accept {
                        identifier: make_identifier!["a"],
                        values: vec![Value::NumericLiteral(1), Value::NumericLiteral(2),],
                    }
                ))
            );
        }

        #[test]
        fn invalid() {
            // Must have accept keyword.
            assert_eq!(
                accept("a { 1 }"),
                Err(nom::Err::Error(BindParserError::AcceptKeyword("a { 1 }".to_string())))
            );

            // Must have identifier.
            assert_eq!(
                accept("accept { 1 }"),
                Err(nom::Err::Error(BindParserError::Identifier("{ 1 }".to_string())))
            );

            // Must have at least one value.
            assert_eq!(
                accept("accept a { }"),
                Err(nom::Err::Error(BindParserError::ConditionValue("}".to_string())))
            );

            // Must delimit blocks with {}s.
            assert_eq!(
                accept("accept a 1 }"),
                Err(nom::Err::Error(BindParserError::ListStart("1 }".to_string())))
            );
            assert_eq!(
                accept("accept a { 1"),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                accept(""),
                Err(nom::Err::Error(BindParserError::AcceptKeyword("".to_string())))
            );
        }
    }

    mod aborts {
        use super::*;

        #[test]
        fn simple() {
            assert_eq!(abort("abort;"), Ok(("", Statement::Abort)));
        }

        #[test]
        fn invalid() {
            // Must have abort keyword.
            assert_eq!(
                abort("a;"),
                Err(nom::Err::Error(BindParserError::AbortKeyword("a;".to_string())))
            );
            assert_eq!(
                abort(";"),
                Err(nom::Err::Error(BindParserError::AbortKeyword(";".to_string())))
            );

            // Must have semicolon.
            assert_eq!(
                abort("abort"),
                Err(nom::Err::Error(BindParserError::Semicolon("".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                abort(""),
                Err(nom::Err::Error(BindParserError::AbortKeyword("".to_string())))
            );
        }
    }

    mod programs {
        use super::*;

        #[test]
        fn simple() {
            assert_eq!(
                program("using a; x == 1;"),
                Ok((
                    "",
                    Ast {
                        using: vec![Include { name: make_identifier!["a"], alias: None }],
                        statements: vec![Statement::ConditionStatement(Condition {
                            lhs: make_identifier!["x"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(1),
                        })]
                    }
                ))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                program(""),
                Err(nom::Err::Error(BindParserError::NoStatements("".to_string())))
            );
        }

        #[test]
        fn requires_statement() {
            // Must have a statement.
            assert_eq!(
                program("using a;"),
                Err(nom::Err::Error(BindParserError::NoStatements("".to_string())))
            );
        }

        #[test]
        fn using_list_optional() {
            assert_eq!(
                program("x == 1;"),
                Ok((
                    "",
                    Ast {
                        using: vec![],
                        statements: vec![Statement::ConditionStatement(Condition {
                            lhs: make_identifier!["x"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(1),
                        })]
                    }
                ))
            );
        }

        #[test]
        fn requires_semicolons() {
            // TODO(fxb/35146): Improve the error type that is returned here.
            assert_eq!(
                program("x == 1"),
                Err(nom::Err::Error(BindParserError::AbortKeyword("x == 1".to_string())))
            );
        }

        #[test]
        fn multiple_statements() {
            assert_eq!(
                program("x == 1; accept y { true } abort; if z == 2 { a != 3; } else { a == 3; }"),
                Ok((
                    "",
                    Ast {
                        using: vec![],
                        statements: vec![
                            Statement::ConditionStatement(Condition {
                                lhs: make_identifier!["x"],
                                op: ConditionOp::Equals,
                                rhs: Value::NumericLiteral(1),
                            }),
                            Statement::Accept {
                                identifier: make_identifier!["y"],
                                values: vec![Value::BoolLiteral(true)],
                            },
                            Statement::Abort,
                            Statement::If {
                                blocks: vec![(
                                    Condition {
                                        lhs: make_identifier!["z"],
                                        op: ConditionOp::Equals,
                                        rhs: Value::NumericLiteral(2),
                                    },
                                    vec![Statement::ConditionStatement(Condition {
                                        lhs: make_identifier!["a"],
                                        op: ConditionOp::NotEquals,
                                        rhs: Value::NumericLiteral(3),
                                    })]
                                )],
                                else_block: vec![Statement::ConditionStatement(Condition {
                                    lhs: make_identifier!["a"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(3),
                                })],
                            }
                        ]
                    }
                ))
            );
        }
    }
}
