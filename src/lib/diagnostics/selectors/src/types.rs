// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics as fdiagnostics;

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
}

#[derive(Debug, Eq, PartialEq)]
pub struct StringPattern<'a>(pub(crate) &'a str);

impl<'a> Into<StringPattern<'a>> for &'a str {
    fn into(self) -> StringPattern<'a> {
        StringPattern(self)
    }
}

#[derive(Debug, Eq, PartialEq)]
pub struct TreeSelector<'a> {
    pub node: Vec<StringPattern<'a>>,
    pub property: Option<StringPattern<'a>>,
}

#[derive(Debug, Eq, PartialEq)]
pub struct ComponentSelector<'a> {
    pub segments: Vec<StringPattern<'a>>,
}

#[derive(Debug, Eq, PartialEq)]
pub struct Selector<'a> {
    pub component: ComponentSelector<'a>,
    pub tree: TreeSelector<'a>,
    pub metadata: Option<MetadataSelector<'a>>,
}

impl Into<fdiagnostics::Selector> for Selector<'_> {
    fn into(self) -> fdiagnostics::Selector {
        fdiagnostics::Selector {
            component_selector: Some(self.component.into()),
            tree_selector: Some(self.tree.into()),
            ..fdiagnostics::Selector::EMPTY // TODO(fxbug.dev/55118): add metadata.
        }
    }
}

impl Into<fdiagnostics::ComponentSelector> for ComponentSelector<'_> {
    fn into(self) -> fdiagnostics::ComponentSelector {
        fdiagnostics::ComponentSelector {
            moniker_segments: Some(
                self.segments.into_iter().map(|segment| segment.into()).collect(),
            ),
            ..fdiagnostics::ComponentSelector::EMPTY
        }
    }
}

impl Into<fdiagnostics::TreeSelector> for TreeSelector<'_> {
    fn into(self) -> fdiagnostics::TreeSelector {
        let node_path = self.node.into_iter().map(|s| s.into()).collect();
        match self.property {
            None => fdiagnostics::TreeSelector::SubtreeSelector(fdiagnostics::SubtreeSelector {
                node_path,
            }),
            Some(property) => {
                fdiagnostics::TreeSelector::PropertySelector(fdiagnostics::PropertySelector {
                    node_path,
                    target_properties: property.into(),
                })
            }
        }
    }
}

impl Into<fdiagnostics::StringSelector> for StringPattern<'_> {
    fn into(self) -> fdiagnostics::StringSelector {
        fdiagnostics::StringSelector::StringPattern(self.0.to_owned())
    }
}
