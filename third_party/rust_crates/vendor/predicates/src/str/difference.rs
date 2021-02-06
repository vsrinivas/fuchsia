// Copyright (c) 2018 The predicates-rs Project Developers.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::borrow;
use std::fmt;

use crate::reflection;
use crate::Predicate;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum DistanceOp {
    Similar,
    Different,
}

impl DistanceOp {
    fn eval(self, limit: i32, distance: i32) -> bool {
        match self {
            DistanceOp::Similar => distance <= limit,
            DistanceOp::Different => limit < distance,
        }
    }
}

/// Predicate that diffs two strings.
///
/// This is created by the `predicate::str::similar`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DifferencePredicate {
    orig: borrow::Cow<'static, str>,
    split: borrow::Cow<'static, str>,
    distance: i32,
    op: DistanceOp,
}

impl DifferencePredicate {
    /// The split used when identifying changes.
    ///
    /// Common splits include:
    /// - `""` for char-level.
    /// - `" "` for word-level.
    /// - `"\n"` for line-level.
    ///
    /// Default: `"\n"`
    ///
    /// # Examples
    ///
    /// ```
    /// use predicates::prelude::*;
    ///
    /// let predicate_fn = predicate::str::similar("Hello World").split(" ");
    /// assert_eq!(true, predicate_fn.eval("Hello World"));
    /// ```
    pub fn split<S>(mut self, split: S) -> Self
    where
        S: Into<borrow::Cow<'static, str>>,
    {
        self.split = split.into();
        self
    }

    /// The maximum allowed edit distance.
    ///
    /// Default: `0`
    ///
    /// # Examples
    ///
    /// ```
    /// use predicates::prelude::*;
    ///
    /// let predicate_fn = predicate::str::similar("Hello World!").split("").distance(1);
    /// assert_eq!(true, predicate_fn.eval("Hello World!"));
    /// assert_eq!(true, predicate_fn.eval("Hello World"));
    /// assert_eq!(false, predicate_fn.eval("Hello World?"));
    /// ```
    pub fn distance(mut self, distance: i32) -> Self {
        self.distance = distance;
        self
    }
}

impl Predicate<str> for DifferencePredicate {
    fn eval(&self, edit: &str) -> bool {
        let change = difference::Changeset::new(&self.orig, edit, &self.split);
        self.op.eval(self.distance, change.distance)
    }

    fn find_case<'a>(&'a self, expected: bool, variable: &str) -> Option<reflection::Case<'a>> {
        let change = difference::Changeset::new(&self.orig, variable, &self.split);
        let result = self.op.eval(self.distance, change.distance);
        if result == expected {
            Some(
                reflection::Case::new(Some(self), result)
                    .add_product(reflection::Product::new("actual distance", change.distance))
                    .add_product(reflection::Product::new("diff", change)),
            )
        } else {
            None
        }
    }
}

impl reflection::PredicateReflection for DifferencePredicate {
    fn parameters<'a>(&'a self) -> Box<dyn Iterator<Item = reflection::Parameter<'a>> + 'a> {
        let params = vec![reflection::Parameter::new("original", &self.orig)];
        Box::new(params.into_iter())
    }
}

impl fmt::Display for DifferencePredicate {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.op {
            DistanceOp::Similar => write!(f, "var - original <= {}", self.distance),
            DistanceOp::Different => write!(f, "{} < var - original", self.distance),
        }
    }
}

/// Creates a new `Predicate` that diffs two strings.
///
/// # Examples
///
/// ```
/// use predicates::prelude::*;
///
/// let predicate_fn = predicate::str::diff("Hello World");
/// assert_eq!(false, predicate_fn.eval("Hello World"));
/// assert_eq!(true, predicate_fn.eval("Goodbye World"));
/// ```
pub fn diff<S>(orig: S) -> DifferencePredicate
where
    S: Into<borrow::Cow<'static, str>>,
{
    DifferencePredicate {
        orig: orig.into(),
        split: "\n".into(),
        distance: 0,
        op: DistanceOp::Different,
    }
}

/// Creates a new `Predicate` that checks strings for how similar they are.
///
/// # Examples
///
/// ```
/// use predicates::prelude::*;
///
/// let predicate_fn = predicate::str::similar("Hello World");
/// assert_eq!(true, predicate_fn.eval("Hello World"));
/// assert_eq!(false, predicate_fn.eval("Goodbye World"));
/// ```
pub fn similar<S>(orig: S) -> DifferencePredicate
where
    S: Into<borrow::Cow<'static, str>>,
{
    DifferencePredicate {
        orig: orig.into(),
        split: "\n".into(),
        distance: 0,
        op: DistanceOp::Similar,
    }
}
