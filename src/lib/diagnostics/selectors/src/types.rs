// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/55118): remove.
#![allow(dead_code)]

/// Severity
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Severity {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
}

/// Identifier
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Identifier {
    Filename,
    LifecycleEventType,
    LineNumber,
    Pid,
    Severity,
    Tags,
    Tid,
    Timestamp,
}

/// Supported comparison operators.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ComparisonOperator {
    Equal,
    GreaterEq,
    Greater,
    LessEq,
    Less,
    NotEq,
}

/// Supported inclusion operators.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InclusionOperator {
    HasAny,
    HasAll,
    In,
}

/// Supported operators.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Operator {
    Inclusion(InclusionOperator),
    Comparison(ComparisonOperator),
}

/// Accepted right-hand-side values that can be used in an operation.
#[derive(Debug, Eq, PartialEq)]
pub enum Value<'a> {
    Severity(Severity),
    StringLiteral(&'a str),
    Number(u64),
}

/// A valid operation and the right hand side of it.
#[derive(Debug, Eq, PartialEq)]
pub enum Operation<'a> {
    Comparison(ComparisonOperator, Value<'a>),
    Inclusion(InclusionOperator, Vec<Value<'a>>),
}

/// Holds a single value or a vector of values of type `T`.
pub(crate) enum OneOrMany<T> {
    One(T),
    Many(Vec<T>),
}

impl<'a> Operation<'a> {
    pub(crate) fn maybe_new(op: Operator, value: OneOrMany<Value<'a>>) -> Option<Self> {
        // Validate the operation can be used with the type of value.
        match (op, value) {
            (Operator::Inclusion(op), OneOrMany::Many(values)) => {
                Some(Operation::Inclusion(op, values))
            }
            (Operator::Inclusion(o @ InclusionOperator::HasAny), OneOrMany::One(value)) => {
                Some(Operation::Inclusion(o, vec![value]))
            }
            (Operator::Inclusion(o @ InclusionOperator::HasAll), OneOrMany::One(value)) => {
                Some(Operation::Inclusion(o, vec![value]))
            }
            (Operator::Comparison(op), OneOrMany::One(value)) => {
                Some(Operation::Comparison(op, value))
            }
            (Operator::Inclusion(InclusionOperator::In), OneOrMany::One(_))
            | (Operator::Comparison(_), OneOrMany::Many(_)) => None,
        }
    }
}

/// A single filter expression in a metadata selector.
#[derive(Debug, Eq, PartialEq)]
pub struct FilterExpression<'a> {
    pub identifier: Identifier,
    pub op: Operation<'a>,
}

/// Represents a  metadata selector, which consists of a list of filters.
#[derive(Debug, Eq, PartialEq)]
pub struct MetadataSelector<'a>(Vec<FilterExpression<'a>>);

impl<'a> MetadataSelector<'a> {
    pub fn new(filters: Vec<FilterExpression<'a>>) -> Self {
        Self(filters)
    }

    pub fn filters(&self) -> &[FilterExpression<'a>] {
        &self.0
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum Segment<'a> {
    Exact(&'a str),
    Pattern(&'a str),
}

#[derive(Debug, Eq, PartialEq)]
pub struct TreeSelector<'a> {
    pub node: Vec<Segment<'a>>,
    pub property: Option<Segment<'a>>,
}

#[derive(Debug, Eq, PartialEq)]
pub struct ComponentSelector<'a> {
    pub segments: Vec<Segment<'a>>,
}

#[derive(Debug, Eq, PartialEq)]
pub struct Selector<'a> {
    pub component: ComponentSelector<'a>,
    pub tree: TreeSelector<'a>,
    pub metadata: Option<MetadataSelector<'a>>,
}
