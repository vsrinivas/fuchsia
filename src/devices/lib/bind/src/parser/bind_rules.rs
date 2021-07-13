// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::parser::common::{
    compound_identifier, condition_value, many_until_eof, map_err, skip_ws, using_list, ws,
    BindParserError, CompoundIdentifier, Include, NomSpan, Span, Value,
};
use nom::{
    branch::alt,
    bytes::complete::tag,
    combinator::{opt, value},
    multi::{many1, separated_nonempty_list},
    sequence::{delimited, preceded, terminated, tuple},
    IResult,
};
use std::convert::TryFrom;

#[derive(Debug, Clone, PartialEq)]
pub struct Ast<'a> {
    pub using: Vec<Include>,
    pub statements: StatementBlock<'a>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ConditionOp {
    Equals,
    NotEquals,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Condition<'a> {
    pub span: Span<'a>,
    pub lhs: CompoundIdentifier,
    pub op: ConditionOp,
    pub rhs: Value,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Statement<'a> {
    ConditionStatement {
        span: Span<'a>,
        condition: Condition<'a>,
    },
    Accept {
        span: Span<'a>,
        identifier: CompoundIdentifier,
        values: Vec<Value>,
    },
    If {
        span: Span<'a>,
        blocks: Vec<(Condition<'a>, StatementBlock<'a>)>,
        else_block: StatementBlock<'a>,
    },
    False {
        span: Span<'a>,
    },
    True {
        span: Span<'a>,
    },
}

pub type StatementBlock<'a> = Vec<Statement<'a>>;

impl<'a> Statement<'a> {
    pub fn get_span(&'a self) -> &'a Span<'a> {
        match self {
            Statement::ConditionStatement { span, .. } => span,
            Statement::Accept { span, .. } => span,
            Statement::If { span, .. } => span,
            Statement::False { span } => span,
            Statement::True { span } => span,
        }
    }
}

// TODO(fxbug.dev/35146): Improve error reporting here.
impl<'a> TryFrom<&'a str> for Ast<'a> {
    type Error = BindParserError;

    fn try_from(input: &'a str) -> Result<Self, Self::Error> {
        match rules(NomSpan::new(input)) {
            Ok((_, ast)) => Ok(ast),
            Err(nom::Err::Error(e)) => Err(e),
            Err(nom::Err::Failure(e)) => Err(e),
            Err(nom::Err::Incomplete(_)) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        }
    }
}

fn condition_op(input: NomSpan) -> IResult<NomSpan, ConditionOp, BindParserError> {
    let equals = value(ConditionOp::Equals, tag("=="));
    let not_equals = value(ConditionOp::NotEquals, tag("!="));
    map_err(alt((equals, not_equals)), BindParserError::ConditionOp)(input)
}

fn condition(input: NomSpan) -> IResult<NomSpan, Condition, BindParserError> {
    let from = skip_ws(input)?;
    let (input, lhs) = ws(compound_identifier)(from)?;
    let (input, op) = ws(condition_op)(input)?;
    let (to, rhs) = ws(condition_value)(input)?;

    let span = Span::from_to(&from, &to);
    Ok((to, Condition { span, lhs, op, rhs }))
}

fn condition_statement(input: NomSpan) -> IResult<NomSpan, Statement, BindParserError> {
    let from = skip_ws(input)?;
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    let (to, condition) = terminated(condition, terminator)(from)?;

    let span = Span::from_to(&from, &to);
    Ok((to, Statement::ConditionStatement { span, condition }))
}

fn keyword_if(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(map_err(tag("if"), BindParserError::IfKeyword))(input)
}

fn keyword_else(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(map_err(tag("else"), BindParserError::ElseKeyword))(input)
}

fn if_statement(input: NomSpan) -> IResult<NomSpan, Statement, BindParserError> {
    let from = skip_ws(input)?;

    let if_block = tuple((preceded(keyword_if, condition), statement_block));
    let if_blocks = separated_nonempty_list(keyword_else, if_block);

    let else_block = preceded(keyword_else, statement_block);

    let (input, blocks) = if_blocks(from)?;
    let (to, else_block) = else_block(input)?;

    let span = Span::from_to(&from, &to);
    Ok((to, Statement::If { span, blocks, else_block }))
}

pub fn statement_block(input: NomSpan) -> IResult<NomSpan, Vec<Statement>, BindParserError> {
    let block_start = ws(map_err(tag("{"), BindParserError::IfBlockStart));
    let block_end = ws(map_err(tag("}"), BindParserError::IfBlockEnd));
    delimited(block_start, many1(ws(statement)), block_end)(input)
}

fn keyword_accept(input: NomSpan) -> IResult<NomSpan, NomSpan, BindParserError> {
    ws(map_err(tag("accept"), BindParserError::AcceptKeyword))(input)
}

fn accept(input: NomSpan) -> IResult<NomSpan, Statement, BindParserError> {
    let from = skip_ws(input)?;

    let list_start = ws(map_err(tag("{"), BindParserError::ListStart));
    let list_end = ws(map_err(tag("}"), BindParserError::ListEnd));
    let separator = || ws(map_err(tag(","), BindParserError::ListSeparator));

    let values = separated_nonempty_list(separator(), ws(condition_value));
    // Lists may optionally be terminated by an additional trailing separator.
    let values = terminated(values, opt(separator()));
    let value_list = delimited(list_start, values, list_end);

    let (to, (identifier, values)) =
        preceded(keyword_accept, tuple((ws(compound_identifier), value_list)))(from)?;

    let span = Span::from_to(&from, &to);
    Ok((to, Statement::Accept { span, identifier, values }))
}

fn keyword_false(input: NomSpan) -> IResult<NomSpan, Statement, BindParserError> {
    let from = skip_ws(input)?;
    let keyword = ws(map_err(tag("false"), BindParserError::FalseKeyword));
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    let (to, _) = terminated(keyword, terminator)(from)?;

    let span = Span::from_to(&from, &to);
    Ok((to, Statement::False { span }))
}

fn keyword_true(input: NomSpan) -> IResult<NomSpan, Statement, BindParserError> {
    let from = skip_ws(input)?;
    let keyword = ws(map_err(tag("true"), BindParserError::TrueKeyword));
    let terminator = ws(map_err(tag(";"), BindParserError::Semicolon));
    let (to, _) = terminated(keyword, terminator)(from)?;

    let span = Span::from_to(&from, &to);
    Ok((to, Statement::True { span }))
}

fn statement(input: NomSpan) -> IResult<NomSpan, Statement, BindParserError> {
    alt((condition_statement, if_statement, accept, keyword_false, keyword_true))(input)
}

fn rules(input: NomSpan) -> IResult<NomSpan, Ast, BindParserError> {
    let (input, using) = ws(using_list)(input)?;
    let (input, statements) = many_until_eof(ws(statement))(input)?;
    if statements.is_empty() {
        return Err(nom::Err::Error(BindParserError::NoStatements(input.to_string())));
    }
    Ok((input, Ast { using, statements }))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;
    use crate::parser::common::test::check_result;

    mod condition_ops {
        use super::*;

        #[test]
        fn equality() {
            check_result(condition_op(NomSpan::new("==")), "", ConditionOp::Equals);
        }

        #[test]
        fn inequality() {
            check_result(condition_op(NomSpan::new("!=")), "", ConditionOp::NotEquals);
        }

        #[test]
        fn invalid() {
            assert_eq!(
                condition_op(NomSpan::new(">=")),
                Err(nom::Err::Error(BindParserError::ConditionOp(">=".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                condition_op(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::ConditionOp("".to_string())))
            );
        }
    }

    mod conditions {
        use super::*;

        #[test]
        fn equality_condition() {
            check_result(
                condition(NomSpan::new("abc == true")),
                "",
                Condition {
                    span: Span { offset: 0, line: 1, fragment: "abc == true" },
                    lhs: make_identifier!["abc"],
                    op: ConditionOp::Equals,
                    rhs: Value::BoolLiteral(true),
                },
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                condition(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::Identifier("".to_string())))
            );
        }

        #[test]
        fn span() {
            // Span doesn't contain leading or trailing whitespace, and offset and line number
            // are correct.
            check_result(
                condition(NomSpan::new(" \n\t\r\nabc \n\t\r\n== \n\t\r\ntrue \n\t\r\n")),
                " \n\t\r\n",
                Condition {
                    span: Span { offset: 5, line: 3, fragment: "abc \n\t\r\n== \n\t\r\ntrue" },
                    lhs: make_identifier!["abc"],
                    op: ConditionOp::Equals,
                    rhs: Value::BoolLiteral(true),
                },
            );
        }
    }

    mod if_statements {
        use super::*;

        #[test]
        fn simple() {
            check_result(
                if_statement(NomSpan::new("if a == b { c == 1; } else { d == 2; }")),
                "",
                Statement::If {
                    span: Span {
                        offset: 0,
                        line: 1,
                        fragment: "if a == b { c == 1; } else { d == 2; }",
                    },
                    blocks: vec![(
                        Condition {
                            span: Span { offset: 3, line: 1, fragment: "a == b" },
                            lhs: make_identifier!["a"],
                            op: ConditionOp::Equals,
                            rhs: Value::Identifier(make_identifier!["b"]),
                        },
                        vec![Statement::ConditionStatement {
                            span: Span { offset: 12, line: 1, fragment: "c == 1;" },
                            condition: Condition {
                                span: Span { offset: 12, line: 1, fragment: "c == 1" },
                                lhs: make_identifier!["c"],
                                op: ConditionOp::Equals,
                                rhs: Value::NumericLiteral(1),
                            },
                        }],
                    )],
                    else_block: vec![Statement::ConditionStatement {
                        span: Span { offset: 29, line: 1, fragment: "d == 2;" },
                        condition: Condition {
                            span: Span { offset: 29, line: 1, fragment: "d == 2" },
                            lhs: make_identifier!["d"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(2),
                        },
                    }],
                },
            );
        }

        #[test]
        fn else_if() {
            check_result(
                if_statement(NomSpan::new(
                    "if a == b { c == 1; } else if e == 3 { d == 2; } else { f != 4; }",
                )),
                "",
                Statement::If {
                    span: Span {
                        offset: 0,
                        line: 1,
                        fragment:
                            "if a == b { c == 1; } else if e == 3 { d == 2; } else { f != 4; }",
                    },
                    blocks: vec![
                        (
                            Condition {
                                span: Span { offset: 3, line: 1, fragment: "a == b" },
                                lhs: make_identifier!["a"],
                                op: ConditionOp::Equals,
                                rhs: Value::Identifier(make_identifier!["b"]),
                            },
                            vec![Statement::ConditionStatement {
                                span: Span { offset: 12, line: 1, fragment: "c == 1;" },
                                condition: Condition {
                                    span: Span { offset: 12, line: 1, fragment: "c == 1" },
                                    lhs: make_identifier!["c"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(1),
                                },
                            }],
                        ),
                        (
                            Condition {
                                span: Span { offset: 30, line: 1, fragment: "e == 3" },
                                lhs: make_identifier!["e"],
                                op: ConditionOp::Equals,
                                rhs: Value::NumericLiteral(3),
                            },
                            vec![Statement::ConditionStatement {
                                span: Span { offset: 39, line: 1, fragment: "d == 2;" },
                                condition: Condition {
                                    span: Span { offset: 39, line: 1, fragment: "d == 2" },
                                    lhs: make_identifier!["d"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(2),
                                },
                            }],
                        ),
                    ],
                    else_block: vec![Statement::ConditionStatement {
                        span: Span { offset: 56, line: 1, fragment: "f != 4;" },
                        condition: Condition {
                            span: Span { offset: 56, line: 1, fragment: "f != 4" },
                            lhs: make_identifier!["f"],
                            op: ConditionOp::NotEquals,
                            rhs: Value::NumericLiteral(4),
                        },
                    }],
                },
            );
        }

        #[test]
        fn nested() {
            check_result(
                if_statement(NomSpan::new(
                    "if a == 1 { if b == 2 { c != 3; } else { c == 3; } } else { d == 2; }",
                )),
                "",
                Statement::If {
                    span: Span {
                        offset: 0,
                        line: 1,
                        fragment:
                            "if a == 1 { if b == 2 { c != 3; } else { c == 3; } } else { d == 2; }",
                    },
                    blocks: vec![(
                        Condition {
                            span: Span { offset: 3, line: 1, fragment: "a == 1" },
                            lhs: make_identifier!["a"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(1),
                        },
                        vec![Statement::If {
                            span: Span {
                                offset: 12,
                                line: 1,
                                fragment: "if b == 2 { c != 3; } else { c == 3; }",
                            },
                            blocks: vec![(
                                Condition {
                                    span: Span { offset: 15, line: 1, fragment: "b == 2" },
                                    lhs: make_identifier!["b"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(2),
                                },
                                vec![Statement::ConditionStatement {
                                    span: Span { offset: 24, line: 1, fragment: "c != 3;" },
                                    condition: Condition {
                                        span: Span { offset: 24, line: 1, fragment: "c != 3" },
                                        lhs: make_identifier!["c"],
                                        op: ConditionOp::NotEquals,
                                        rhs: Value::NumericLiteral(3),
                                    },
                                }],
                            )],
                            else_block: vec![Statement::ConditionStatement {
                                span: Span { offset: 41, line: 1, fragment: "c == 3;" },
                                condition: Condition {
                                    span: Span { offset: 41, line: 1, fragment: "c == 3" },
                                    lhs: make_identifier!["c"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(3),
                                },
                            }],
                        }],
                    )],
                    else_block: vec![Statement::ConditionStatement {
                        span: Span { offset: 60, line: 1, fragment: "d == 2;" },
                        condition: Condition {
                            span: Span { offset: 60, line: 1, fragment: "d == 2" },
                            lhs: make_identifier!["d"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(2),
                        },
                    }],
                },
            );
        }

        #[test]
        fn invalid() {
            // Must have 'if' keyword.
            assert_eq!(
                if_statement(NomSpan::new("a == b { c == 1; }")),
                Err(nom::Err::Error(BindParserError::IfKeyword("a == b { c == 1; }".to_string())))
            );

            // Must have condition.
            assert_eq!(
                if_statement(NomSpan::new("if { c == 1; }")),
                Err(nom::Err::Error(BindParserError::Identifier("{ c == 1; }".to_string())))
            );

            // Must have else block.
            assert_eq!(
                if_statement(NomSpan::new("if a == b { c == 1; }")),
                Err(nom::Err::Error(BindParserError::ElseKeyword("".to_string())))
            );
            assert_eq!(
                if_statement(NomSpan::new("if a == b { c == 1; } else if e == 3 { d == 2; }")),
                Err(nom::Err::Error(BindParserError::ElseKeyword("".to_string())))
            );

            // Must delimit blocks with {}s.
            assert_eq!(
                if_statement(NomSpan::new("if a == b c == 1; }")),
                Err(nom::Err::Error(BindParserError::IfBlockStart("c == 1; }".to_string())))
            );
            assert_eq!(
                if_statement(NomSpan::new("if a == b { c == 1;")),
                Err(nom::Err::Error(BindParserError::IfBlockEnd("".to_string())))
            );

            // Blocks must not be empty.
            // TODO(fxbug.dev/35146): Improve this error message, it currently reports a failure to parse
            // an accept statement due to the way the combinator works for the statement parser.
            assert!(if_statement(NomSpan::new("if a == b { } else { c == 1; }")).is_err());
            assert!(if_statement(NomSpan::new("if a == b { c == 1; } else { }")).is_err());
        }

        #[test]
        fn empty() {
            assert_eq!(
                if_statement(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::IfKeyword("".to_string())))
            );
        }
    }

    mod accepts {
        use super::*;

        #[test]
        fn simple() {
            check_result(
                accept(NomSpan::new("accept a { 1 }")),
                "",
                Statement::Accept {
                    span: Span { offset: 0, line: 1, fragment: "accept a { 1 }" },
                    identifier: make_identifier!["a"],
                    values: vec![Value::NumericLiteral(1)],
                },
            );
        }

        #[test]
        fn multiple_values() {
            check_result(
                accept(NomSpan::new("accept a { 1, 2 }")),
                "",
                Statement::Accept {
                    span: Span { offset: 0, line: 1, fragment: "accept a { 1, 2 }" },
                    identifier: make_identifier!["a"],
                    values: vec![Value::NumericLiteral(1), Value::NumericLiteral(2)],
                },
            );
        }

        #[test]
        fn trailing_comma() {
            check_result(
                accept(NomSpan::new("accept a { 1, 2, }")),
                "",
                Statement::Accept {
                    span: Span { offset: 0, line: 1, fragment: "accept a { 1, 2, }" },
                    identifier: make_identifier!["a"],
                    values: vec![Value::NumericLiteral(1), Value::NumericLiteral(2)],
                },
            );
        }

        #[test]
        fn invalid() {
            // Must have accept keyword.
            assert_eq!(
                accept(NomSpan::new("a { 1 }")),
                Err(nom::Err::Error(BindParserError::AcceptKeyword("a { 1 }".to_string())))
            );

            // Must have identifier.
            assert_eq!(
                accept(NomSpan::new("accept { 1 }")),
                Err(nom::Err::Error(BindParserError::Identifier("{ 1 }".to_string())))
            );

            // Must have at least one value.
            assert_eq!(
                accept(NomSpan::new("accept a { }")),
                Err(nom::Err::Error(BindParserError::ConditionValue("}".to_string())))
            );

            // Must delimit blocks with {}s.
            assert_eq!(
                accept(NomSpan::new("accept a 1 }")),
                Err(nom::Err::Error(BindParserError::ListStart("1 }".to_string())))
            );
            assert_eq!(
                accept(NomSpan::new("accept a { 1")),
                Err(nom::Err::Error(BindParserError::ListEnd("".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                accept(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::AcceptKeyword("".to_string())))
            );
        }

        #[test]
        fn span() {
            // Span doesn't contain leading or trailing whitespace, and line number is correct.
            check_result(
                accept(NomSpan::new(" \n\t\r\naccept a \n\t\r\n{ 1 } \n\t\r\n")),
                " \n\t\r\n",
                Statement::Accept {
                    span: Span { offset: 5, line: 3, fragment: "accept a \n\t\r\n{ 1 }" },
                    identifier: make_identifier!["a"],
                    values: vec![Value::NumericLiteral(1)],
                },
            );
        }
    }

    mod false_statement {
        use super::*;

        #[test]
        fn simple() {
            check_result(
                keyword_false(NomSpan::new("false;")),
                "",
                Statement::False { span: Span { offset: 0, line: 1, fragment: "false;" } },
            );
        }

        #[test]
        fn invalid() {
            // Must have false keyword.
            assert_eq!(
                keyword_false(NomSpan::new("a;")),
                Err(nom::Err::Error(BindParserError::FalseKeyword("a;".to_string())))
            );
            assert_eq!(
                keyword_false(NomSpan::new(";")),
                Err(nom::Err::Error(BindParserError::FalseKeyword(";".to_string())))
            );

            // Must have semicolon.
            assert_eq!(
                keyword_false(NomSpan::new("false")),
                Err(nom::Err::Error(BindParserError::Semicolon("".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                keyword_false(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::FalseKeyword("".to_string())))
            );
        }

        #[test]
        fn span() {
            // Span doesn't contain leading or trailing whitespace, and line number is correct.
            check_result(
                keyword_false(NomSpan::new(" \n\t\r\nfalse; \n\t\r\n")),
                " \n\t\r\n",
                Statement::False { span: Span { offset: 5, line: 3, fragment: "false;" } },
            );
        }
    }

    mod true_statement {
        use super::*;

        #[test]
        fn simple() {
            check_result(
                keyword_true(NomSpan::new("true;")),
                "",
                Statement::True { span: Span { offset: 0, line: 1, fragment: "true;" } },
            );
        }

        #[test]
        fn invalid() {
            // Must have true keyword.
            assert_eq!(
                keyword_true(NomSpan::new("p;")),
                Err(nom::Err::Error(BindParserError::TrueKeyword("p;".to_string())))
            );
            assert_eq!(
                keyword_true(NomSpan::new(";")),
                Err(nom::Err::Error(BindParserError::TrueKeyword(";".to_string())))
            );

            // Must have semicolon.
            assert_eq!(
                keyword_true(NomSpan::new("true")),
                Err(nom::Err::Error(BindParserError::Semicolon("".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                keyword_true(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::TrueKeyword("".to_string())))
            );
        }

        #[test]
        fn span() {
            // Span doesn't contain leading or trailing whitespace, and line number is correct.
            check_result(
                keyword_true(NomSpan::new(" \n\t\r\ntrue; \n\t\r\n")),
                " \n\t\r\n",
                Statement::True { span: Span { offset: 5, line: 3, fragment: "true;" } },
            );
        }
    }

    mod rules {
        use super::*;

        #[test]
        fn simple() {
            check_result(
                rules(NomSpan::new("using a; x == 1;")),
                "",
                Ast {
                    using: vec![Include { name: make_identifier!["a"], alias: None }],
                    statements: vec![Statement::ConditionStatement {
                        span: Span { offset: 9, line: 1, fragment: "x == 1;" },
                        condition: Condition {
                            span: Span { offset: 9, line: 1, fragment: "x == 1" },
                            lhs: make_identifier!["x"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(1),
                        },
                    }],
                },
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                rules(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::NoStatements("".to_string())))
            );
        }

        #[test]
        fn requires_statement() {
            // Must have a statement.
            assert_eq!(
                rules(NomSpan::new("using a;")),
                Err(nom::Err::Error(BindParserError::NoStatements("".to_string())))
            );
        }

        #[test]
        fn using_list_optional() {
            check_result(
                rules(NomSpan::new("x == 1;")),
                "",
                Ast {
                    using: vec![],
                    statements: vec![Statement::ConditionStatement {
                        span: Span { offset: 0, line: 1, fragment: "x == 1;" },
                        condition: Condition {
                            span: Span { offset: 0, line: 1, fragment: "x == 1" },
                            lhs: make_identifier!["x"],
                            op: ConditionOp::Equals,
                            rhs: Value::NumericLiteral(1),
                        },
                    }],
                },
            );
        }

        #[test]
        fn requires_semicolons() {
            // TODO(fxbug.dev/35146): Improve the error type that is returned here.
            assert_eq!(
                rules(NomSpan::new("x == 1")),
                Err(nom::Err::Error(BindParserError::TrueKeyword("x == 1".to_string())))
            );
        }

        #[test]
        fn multiple_statements() {
            check_result(
                rules(NomSpan::new(
                    "x == 1; accept y { true } false; if z == 2 { a != 3; } else { a == 3; }",
                )),
                "",
                Ast {
                    using: vec![],
                    statements: vec![
                        Statement::ConditionStatement {
                            span: Span { offset: 0, line: 1, fragment: "x == 1;" },
                            condition: Condition {
                                span: Span { offset: 0, line: 1, fragment: "x == 1" },
                                lhs: make_identifier!["x"],
                                op: ConditionOp::Equals,
                                rhs: Value::NumericLiteral(1),
                            },
                        },
                        Statement::Accept {
                            span: Span { offset: 8, line: 1, fragment: "accept y { true }" },
                            identifier: make_identifier!["y"],
                            values: vec![Value::BoolLiteral(true)],
                        },
                        Statement::False { span: Span { offset: 26, line: 1, fragment: "false;" } },
                        Statement::If {
                            span: Span {
                                offset: 33,
                                line: 1,
                                fragment: "if z == 2 { a != 3; } else { a == 3; }",
                            },
                            blocks: vec![(
                                Condition {
                                    span: Span { offset: 36, line: 1, fragment: "z == 2" },
                                    lhs: make_identifier!["z"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(2),
                                },
                                vec![Statement::ConditionStatement {
                                    span: Span { offset: 45, line: 1, fragment: "a != 3;" },
                                    condition: Condition {
                                        span: Span { offset: 45, line: 1, fragment: "a != 3" },
                                        lhs: make_identifier!["a"],
                                        op: ConditionOp::NotEquals,
                                        rhs: Value::NumericLiteral(3),
                                    },
                                }],
                            )],
                            else_block: vec![Statement::ConditionStatement {
                                span: Span { offset: 62, line: 1, fragment: "a == 3;" },
                                condition: Condition {
                                    span: Span { offset: 62, line: 1, fragment: "a == 3" },
                                    lhs: make_identifier!["a"],
                                    op: ConditionOp::Equals,
                                    rhs: Value::NumericLiteral(3),
                                },
                            }],
                        },
                    ],
                },
            );
        }
    }
}
