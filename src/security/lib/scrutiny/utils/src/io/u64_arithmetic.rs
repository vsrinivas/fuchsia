// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    std::fmt::{Debug, Display, Formatter},
    thiserror::Error,
};

/// Safe computation of `-i64::MIN as u64` without overflow.
pub(crate) const NEGATIVE_I64_MIN_AS_U64: u64 = -(i64::MIN as i128) as u64;

/// Safely compute the absolute value of an `i64` as a `u64`.
#[inline]
pub(crate) fn abs(i: i64) -> u64 {
    if i >= 0 {
        i as u64
    } else if i == i64::MIN {
        NEGATIVE_I64_MIN_AS_U64
    } else {
        -i as u64
    }
}

/// Modeled custom errors that can be encountered performing u64 arithmetic operations.
#[derive(Debug, Error)]
#[error("u64 bounds check arithmetic overflow/underflow: {lhs} {op} {rhs}")]
pub(crate) struct U64ArithmeticError {
    lhs: u64,
    op: U64Operation,
    rhs: u64,
}

#[cfg(test)]
impl U64ArithmeticError {
    pub fn new(lhs: u64, op: U64Operation, rhs: u64) -> Self {
        Self { lhs, op, rhs }
    }
}

pub(crate) trait U64Eval: Clone + Display {
    fn eval(&self) -> Result<u64>;
}

#[inline]
pub(crate) fn named_u64(name: &'static str, value: u64) -> NamedU64 {
    NamedU64 { name, value }
}

/// Model a named numeric value in arithmetic expressions.
#[derive(Clone)]
pub(crate) struct NamedU64 {
    name: &'static str,
    value: u64,
}

impl Display for NamedU64 {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
        f.write_fmt(format_args!("{}={}", self.name, self.value))
    }
}

impl U64Eval for NamedU64 {
    #[inline]
    fn eval(&self) -> Result<u64> {
        Ok(self.value)
    }
}

/// Model an operation in arithmetic expressions.
#[derive(Clone)]
pub(crate) enum U64Operation {
    Add,
    Subtract,
}

impl U64Operation {
    fn fmt_operation(&self, f: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
        match self {
            Self::Add => f.write_str("+"),
            Self::Subtract => f.write_str("-"),
        }
    }
}

impl Display for U64Operation {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
        self.fmt_operation(f)
    }
}

impl Debug for U64Operation {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
        self.fmt_operation(f)
    }
}

#[inline]
pub(crate) fn u64_add<'lhs, 'rhs, LHS: U64Eval, RHS: U64Eval>(
    lhs: &'lhs LHS,
    rhs: &'rhs RHS,
) -> U64Expression<'lhs, 'rhs, LHS, RHS> {
    U64Expression { lhs, op: U64Operation::Add, rhs }
}

#[inline]
pub(crate) fn u64_sub<'lhs, 'rhs, LHS: U64Eval, RHS: U64Eval>(
    lhs: &'lhs LHS,
    rhs: &'rhs RHS,
) -> U64Expression<'lhs, 'rhs, LHS, RHS> {
    U64Expression { lhs, op: U64Operation::Subtract, rhs }
}

/// Model an evaluable arithmetic expression `<evaluable> <operation> <evaluable>`.
#[derive(Clone)]
pub(crate) struct U64Expression<'lhs, 'rhs, LHS: U64Eval, RHS: U64Eval> {
    lhs: &'lhs LHS,
    op: U64Operation,
    rhs: &'rhs RHS,
}

impl<'lhs, 'rhs, LHS: U64Eval, RHS: U64Eval> U64Expression<'lhs, 'rhs, LHS, RHS> {
    pub fn context(&self) -> String {
        format!("in {}", self)
    }
}

impl<'lhs, 'rhs, LHS: U64Eval, RHS: U64Eval> Display for U64Expression<'lhs, 'rhs, LHS, RHS> {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
        f.write_fmt(format_args!("{} {} {}", self.lhs, self.op, self.rhs))
    }
}

impl<'lhs, 'rhs, LHS: U64Eval, RHS: U64Eval> U64Eval for U64Expression<'lhs, 'rhs, LHS, RHS> {
    #[inline]
    fn eval(&self) -> Result<u64> {
        let lhs = self.lhs.eval().with_context(|| self.context())?;
        let rhs = self.rhs.eval().with_context(|| self.context())?;
        match self.op {
            U64Operation::Add => lhs
                .checked_add(rhs)
                .ok_or_else(|| U64ArithmeticError { lhs, op: U64Operation::Add, rhs })
                .with_context(|| self.context()),
            U64Operation::Subtract => lhs
                .checked_sub(rhs)
                .ok_or_else(|| U64ArithmeticError { lhs, op: U64Operation::Subtract, rhs })
                .with_context(|| self.context()),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{named_u64, u64_add, u64_sub, U64ArithmeticError, U64Eval, U64Operation},
        anyhow::Context,
    };

    #[fuchsia::test]
    fn add_ok() {
        let zero = named_u64("zero", 0);
        let u64_max = named_u64("u64_max", u64::MAX);
        assert_eq!(u64_add(&zero, &u64_max).eval().unwrap(), u64::MAX);
    }

    #[fuchsia::test]
    fn sub_ok() {
        let u64_max = named_u64("u64_max", u64::MAX);
        assert_eq!(u64_sub(&u64_max, &u64_max).eval().unwrap(), 0);
    }

    #[fuchsia::test]
    fn compound_add_ok() {
        let zero = named_u64("zero", 0);
        let u64_max_minus_one = named_u64("u64_max_minus_one", u64::MAX - 1);
        let one = named_u64("one", 1);
        let inner = u64_add(&zero, &u64_max_minus_one);
        assert_eq!(u64_add(&inner, &one).eval().unwrap(), u64::MAX);
    }

    #[fuchsia::test]
    fn compound_sub_ok() {
        let u64_max = named_u64("u64_max", u64::MAX);
        let u64_max_minus_one = named_u64("u64_max_minus_one", u64::MAX - 1);
        let one = named_u64("one", 1);
        let inner = u64_sub(&u64_max, &u64_max_minus_one);
        assert_eq!(u64_sub(&inner, &one).eval().unwrap(), 0);
    }

    #[fuchsia::test]
    fn compound_mixed_ok() {
        let zero = named_u64("zero", 0);
        let u64_max = named_u64("u64_max", u64::MAX);
        let inner = u64_add(&zero, &u64_max);
        assert_eq!(u64_sub(&inner, &u64_max).eval().unwrap(), 0);
    }

    fn assert_anyhow_error(actual: anyhow::Error, expected: anyhow::Error) {
        let actual_debug = format!("{:?}", actual);
        let expected_debug = format!("{:?}", expected);
        assert_eq!(actual_debug, expected_debug);
        let actual_display = format!("{}", actual);
        let expected_display = format!("{}", expected);
        assert_eq!(actual_display, expected_display);
    }

    #[fuchsia::test]
    fn add_err() {
        let one = named_u64("one", 1);
        let u64_max = named_u64("u64_max", u64::MAX);
        let error_expression = u64_add(&one, &u64_max);

        let expected_result: anyhow::Result<usize, U64ArithmeticError> =
            Err(U64ArithmeticError::new(1, U64Operation::Add, u64::MAX));
        let expected_contextualized_result = expected_result.context(error_expression.context());
        let expected_error = expected_contextualized_result.err().unwrap();

        assert_anyhow_error(error_expression.eval().err().unwrap(), expected_error);
    }

    #[fuchsia::test]
    fn sub_err() {
        let u64_max_mius_1 = named_u64("u64_max_mius_1", u64::MAX - 1);
        let u64_max = named_u64("u64_max", u64::MAX);
        let error_expression = u64_sub(&u64_max_mius_1, &u64_max);

        let expected_result: anyhow::Result<usize, U64ArithmeticError> =
            Err(U64ArithmeticError::new(u64::MAX - 1, U64Operation::Subtract, u64::MAX));
        let expected_contextualized_result = expected_result.context(error_expression.context());
        let expected_error = expected_contextualized_result.err().unwrap();

        assert_anyhow_error(error_expression.eval().err().unwrap(), expected_error);
    }

    #[fuchsia::test]
    fn mixed_err() {
        let zero = named_u64("zero", 0);
        let one = named_u64("one", 1);
        let two = named_u64("two", 2);
        let inner = u64_add(&zero, &one);
        let error_expression = u64_sub(&inner, &two);

        let expected_result: anyhow::Result<usize, U64ArithmeticError> =
            Err(U64ArithmeticError::new(1, U64Operation::Subtract, 2));
        let expected_contextualized_result = expected_result.context(error_expression.context());
        let expected_error = expected_contextualized_result.err().unwrap();

        assert_anyhow_error(error_expression.eval().err().unwrap(), expected_error);
    }
}
