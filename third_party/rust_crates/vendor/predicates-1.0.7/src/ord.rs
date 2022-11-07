// Copyright (c) 2018 The predicates-rs Project Developers.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/license/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! Definition of `Predicate`s for comparisons over `Ord` and `Eq` types.

use std::fmt;

use crate::reflection;
use crate::utils;
use crate::Predicate;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum EqOps {
    Equal,
    NotEqual,
}

impl fmt::Display for EqOps {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let op = match *self {
            EqOps::Equal => "==",
            EqOps::NotEqual => "!=",
        };
        write!(f, "{}", op)
    }
}

/// Predicate that returns `true` if `variable` matches the pre-defined `Eq`
/// value, otherwise returns `false`.
///
/// This is created by the `predicate::{eq, ne}` functions.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EqPredicate<T>
where
    T: fmt::Debug + PartialEq,
{
    constant: T,
    op: EqOps,
}

impl<T> Predicate<T> for EqPredicate<T>
where
    T: fmt::Debug + PartialEq,
{
    fn eval(&self, variable: &T) -> bool {
        match self.op {
            EqOps::Equal => variable.eq(&self.constant),
            EqOps::NotEqual => variable.ne(&self.constant),
        }
    }

    fn find_case<'a>(&'a self, expected: bool, variable: &T) -> Option<reflection::Case<'a>> {
        utils::default_find_case(self, expected, variable)
    }
}

impl<'a, T> Predicate<T> for EqPredicate<&'a T>
where
    T: fmt::Debug + PartialEq + ?Sized,
{
    fn eval(&self, variable: &T) -> bool {
        match self.op {
            EqOps::Equal => variable.eq(self.constant),
            EqOps::NotEqual => variable.ne(self.constant),
        }
    }

    fn find_case<'b>(&'b self, expected: bool, variable: &T) -> Option<reflection::Case<'b>> {
        utils::default_find_case(self, expected, variable)
    }
}

impl<T> reflection::PredicateReflection for EqPredicate<T> where T: fmt::Debug + PartialEq {}

impl<T> fmt::Display for EqPredicate<T>
where
    T: fmt::Debug + PartialEq,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "var {} {:?}", self.op, self.constant)
    }
}

/// Creates a new predicate that will return `true` when the given `variable` is
/// equal to a pre-defined value.
///
/// # Examples
///
/// ```
/// use predicates::prelude::*;
///
/// let predicate_fn = predicate::eq(5);
/// assert_eq!(true, predicate_fn.eval(&5));
/// assert_eq!(false, predicate_fn.eval(&10));
///
/// let predicate_fn = predicate::eq("Hello");
/// assert_eq!(true, predicate_fn.eval("Hello"));
/// assert_eq!(false, predicate_fn.eval("Goodbye"));
/// ```
pub fn eq<T>(constant: T) -> EqPredicate<T>
where
    T: fmt::Debug + PartialEq,
{
    EqPredicate {
        constant,
        op: EqOps::Equal,
    }
}

/// Creates a new predicate that will return `true` when the given `variable` is
/// _not_ equal to a pre-defined value.
///
/// # Examples
///
/// ```
/// use predicates::prelude::*;
///
/// let predicate_fn = predicate::ne(5);
/// assert_eq!(false, predicate_fn.eval(&5));
/// assert_eq!(true, predicate_fn.eval(&10));
/// ```
pub fn ne<T>(constant: T) -> EqPredicate<T>
where
    T: PartialEq + fmt::Debug,
{
    EqPredicate {
        constant,
        op: EqOps::NotEqual,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum OrdOps {
    LessThan,
    LessThanOrEqual,
    GreaterThanOrEqual,
    GreaterThan,
}

impl fmt::Display for OrdOps {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let op = match *self {
            OrdOps::LessThan => "<",
            OrdOps::LessThanOrEqual => "<=",
            OrdOps::GreaterThanOrEqual => ">=",
            OrdOps::GreaterThan => ">",
        };
        write!(f, "{}", op)
    }
}

/// Predicate that returns `true` if `variable` matches the pre-defined `Ord`
/// value, otherwise returns `false`.
///
/// This is created by the `predicate::{gt, ge, lt, le}` functions.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct OrdPredicate<T>
where
    T: fmt::Debug + PartialOrd,
{
    constant: T,
    op: OrdOps,
}

impl<T> Predicate<T> for OrdPredicate<T>
where
    T: fmt::Debug + PartialOrd,
{
    fn eval(&self, variable: &T) -> bool {
        match self.op {
            OrdOps::LessThan => variable.lt(&self.constant),
            OrdOps::LessThanOrEqual => variable.le(&self.constant),
            OrdOps::GreaterThanOrEqual => variable.ge(&self.constant),
            OrdOps::GreaterThan => variable.gt(&self.constant),
        }
    }

    fn find_case<'a>(&'a self, expected: bool, variable: &T) -> Option<reflection::Case<'a>> {
        utils::default_find_case(self, expected, variable)
    }
}

impl<'a, T> Predicate<T> for OrdPredicate<&'a T>
where
    T: fmt::Debug + PartialOrd + ?Sized,
{
    fn eval(&self, variable: &T) -> bool {
        match self.op {
            OrdOps::LessThan => variable.lt(self.constant),
            OrdOps::LessThanOrEqual => variable.le(self.constant),
            OrdOps::GreaterThanOrEqual => variable.ge(self.constant),
            OrdOps::GreaterThan => variable.gt(self.constant),
        }
    }

    fn find_case<'b>(&'b self, expected: bool, variable: &T) -> Option<reflection::Case<'b>> {
        utils::default_find_case(self, expected, variable)
    }
}

impl<T> reflection::PredicateReflection for OrdPredicate<T> where T: fmt::Debug + PartialOrd {}

impl<T> fmt::Display for OrdPredicate<T>
where
    T: fmt::Debug + PartialOrd,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "var {} {:?}", self.op, self.constant)
    }
}

/// Creates a new predicate that will return `true` when the given `variable` is
/// less than a pre-defined value.
///
/// # Examples
///
/// ```
/// use predicates::prelude::*;
///
/// let predicate_fn = predicate::lt(5);
/// assert_eq!(true, predicate_fn.eval(&4));
/// assert_eq!(false, predicate_fn.eval(&6));
///
/// let predicate_fn = predicate::lt("b");
/// assert_eq!(true, predicate_fn.eval("a"));
/// assert_eq!(false, predicate_fn.eval("c"));
/// ```
pub fn lt<T>(constant: T) -> OrdPredicate<T>
where
    T: fmt::Debug + PartialOrd,
{
    OrdPredicate {
        constant,
        op: OrdOps::LessThan,
    }
}

/// Creates a new predicate that will return `true` when the given `variable` is
/// less than or equal to a pre-defined value.
///
/// # Examples
///
/// ```
/// use predicates::prelude::*;
///
/// let predicate_fn = predicate::le(5);
/// assert_eq!(true, predicate_fn.eval(&4));
/// assert_eq!(true, predicate_fn.eval(&5));
/// assert_eq!(false, predicate_fn.eval(&6));
/// ```
pub fn le<T>(constant: T) -> OrdPredicate<T>
where
    T: PartialOrd + fmt::Debug,
{
    OrdPredicate {
        constant,
        op: OrdOps::LessThanOrEqual,
    }
}

/// Creates a new predicate that will return `true` when the given `variable` is
/// greater than or equal to a pre-defined value.
///
/// # Examples
///
/// ```
/// use predicates::prelude::*;
///
/// let predicate = predicate::ge(5);
/// assert_eq!(false, predicate.eval(&4));
/// assert_eq!(true, predicate.eval(&5));
/// assert_eq!(true, predicate.eval(&6));
/// ```
pub fn ge<T>(constant: T) -> OrdPredicate<T>
where
    T: PartialOrd + fmt::Debug,
{
    OrdPredicate {
        constant,
        op: OrdOps::GreaterThanOrEqual,
    }
}

/// Creates a new predicate that will return `true` when the given `variable` is
/// greater than a pre-defined value.
///
/// # Examples
///
/// ```
/// use predicates::prelude::*;
///
/// let predicate_fn = predicate::gt(5);
/// assert_eq!(false, predicate_fn.eval(&4));
/// assert_eq!(false, predicate_fn.eval(&5));
/// assert_eq!(true, predicate_fn.eval(&6));
/// ```
pub fn gt<T>(constant: T) -> OrdPredicate<T>
where
    T: PartialOrd + fmt::Debug,
{
    OrdPredicate {
        constant,
        op: OrdOps::GreaterThan,
    }
}
