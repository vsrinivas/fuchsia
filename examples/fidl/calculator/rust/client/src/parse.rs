// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple parser for calculator expressions.
//!
//! This parser is intentionally not as fully featured as it could be, electing
//! to not support more complex expressions to aid in readability. It's just
//! provided to make it a bit easier to add expressions via
//! [input.txt](input.txt).
use nom::branch::alt;
use nom::bytes::complete::tag;
use nom::character::complete::space0;
use nom::combinator::map;
use nom::number::complete::double;
use nom::sequence::{delimited, tuple};
use nom::IResult;

#[derive(Debug, PartialEq)]
pub(crate) enum Operator {
    Add,
    Subtract,
    Multiply,
    Divide,
    Pow,
}

/// This enum is intentionally overly simplified to make the example easier to
/// understand.
#[derive(Debug, PartialEq)]
pub(crate) enum Expression {
    Leaf(f64, Operator, f64),
}

/// Parses an Operator
fn operator(s: &str) -> IResult<&str, Operator> {
    alt((
        map(tag("+"), |_: &str| Operator::Add),
        map(tag("-"), |_: &str| Operator::Subtract),
        map(tag("*"), |_: &str| Operator::Multiply),
        map(tag("/"), |_: &str| Operator::Divide),
        map(tag("^"), |_: &str| Operator::Pow),
    ))(s)
}

/// Parses a float64. Allows one or more spaces before and after the number.
fn number(s: &str) -> IResult<&str, f64> {
    map(delimited(space0, double, space0), |n| n)(s)
}

/// Parses an Expression::Leaf.
fn leaf(s: &str) -> IResult<&str, Expression> {
    map(tuple((number, operator, number)), |(left, op, right)| Expression::Leaf(left, op, right))(s)
}

/// Parses an Expression.
fn expression(s: &str) -> IResult<&str, Expression> {
    // Uses leaf, leaf as the tuple to make it easier to support more complex
    // Expression enum variants in the future.
    alt((leaf, leaf))(s)
}

/// Given a string, parses an Expression. Panics if the parse is unsuccessful.
pub(crate) fn parse(s: &str) -> Expression {
    let (_, exp) = expression(s).expect("parse error");
    exp
}

#[cfg(test)]
mod tests {
    use crate::parse::*;

    #[test]
    fn parse_operators() {
        let input = "+";
        let (_, actual) = operator(input).expect("Parse error");
        assert_eq!(actual, Operator::Add);

        let input = "-";
        let (_, actual) = operator(input).expect("Parse error");
        assert_eq!(actual, Operator::Subtract);

        let input = "*";
        let (_, actual) = operator(input).expect("Parse error");
        assert_eq!(actual, Operator::Multiply);

        let input = "/";
        let (_, actual) = operator(input).expect("Parse error");
        assert_eq!(actual, Operator::Divide);

        let input = "^";
        let (_, actual) = operator(input).expect("Parse error");
        assert_eq!(actual, Operator::Pow);
    }

    #[test]
    fn parse_expression() {
        let input = "1 + 2";
        let (_, actual) = expression(input).expect("Parse error");
        assert_eq!(actual, Expression::Leaf(1.0, Operator::Add, 2.0));
    }

    #[test]
    fn parse_expression_with_negative_numbers() {
        let input = "-1 + -2";
        let (_, actual) = expression(input).expect("Parse error");
        assert_eq!(actual, Expression::Leaf(-1.0, Operator::Add, -2.0));
    }

    #[test]
    fn parse_expression_with_subtraction() {
        let input = "-1.234 - -2.345";
        let (_, actual) = expression(input).expect("Parse error");
        assert_eq!(actual, Expression::Leaf(-1.234, Operator::Subtract, -2.345));
    }

    #[test]
    fn parse_expression_with_multiplication() {
        let input = "1.5 * 2.0";
        let (_, actual) = expression(input).expect("Parse error");
        assert_eq!(actual, Expression::Leaf(1.5, Operator::Multiply, 2.0));
    }

    #[test]
    fn parse_expression_with_division() {
        let input = "1.5 / 3.0";
        let (_, actual) = expression(input).expect("Parse error");
        assert_eq!(actual, Expression::Leaf(1.5, Operator::Divide, 3.0));
    }

    #[test]
    fn parse_expression_with_pow() {
        let input = "2.0 ^ 4.5";
        let (_, actual) = expression(input).expect("Parse error");
        assert_eq!(actual, Expression::Leaf(2.0, Operator::Pow, 4.5));
    }
}
