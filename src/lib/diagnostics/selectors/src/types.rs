// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics as fdiagnostics;
use std::{borrow::Cow, fmt::Debug};

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
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Value<'a> {
    Severity(Severity),
    StringLiteral(&'a str),
    Number(u64),
}

impl Value<'_> {
    fn ty(&self) -> ValueType {
        match self {
            Value::Severity(_) => ValueType::Severity,
            Value::StringLiteral(_) => ValueType::StringLiteral,
            Value::Number(_) => ValueType::Number,
        }
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum ValueType {
    Severity,
    StringLiteral,
    Number,
}

/// Holds a single value or a vector of values of type `T`.
#[derive(Debug, Eq, PartialEq)]
pub enum OneOrMany<T: Debug + Eq + PartialEq> {
    One(T),
    Many(Vec<T>),
}

impl OneOrMany<Value<'_>> {
    pub(crate) fn ty(&self) -> OneOrMany<ValueType> {
        match self {
            Self::One(value) => OneOrMany::One(value.ty()),
            Self::Many(values) => OneOrMany::Many(values.into_iter().map(|v| v.ty()).collect()),
        }
    }
}

/// A single filter expression in a metadata selector.
#[derive(Debug, Eq, PartialEq)]
pub struct FilterExpression<'a> {
    pub identifier: Identifier,
    pub operator: Operator,
    pub value: OneOrMany<Value<'a>>,
}

/// Represents a  metadata selector, which consists of a list of filters.
#[derive(Debug, Eq, PartialEq)]
pub struct MetadataSelector<'a>(pub(crate) Vec<FilterExpression<'a>>);

impl<'a> MetadataSelector<'a> {
    pub fn new(filters: Vec<FilterExpression<'a>>) -> Self {
        Self(filters)
    }

    pub fn filters(&self) -> &[FilterExpression<'a>] {
        self.0.as_slice()
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum Segment<'a> {
    ExactMatch(Cow<'a, str>),
    Pattern(&'a str),
}

fn contains_unescaped_wildcard(s: &str) -> bool {
    let mut iter = s.chars();
    while let Some(c) = iter.next() {
        match c {
            '*' => return true,
            '\\' => {
                // skip escaped characters
                let _ = iter.next();
            }
            _ => {}
        }
    }
    false
}

impl<'a> Into<Segment<'a>> for &'a str {
    fn into(self) -> Segment<'a> {
        if contains_unescaped_wildcard(self) {
            return Segment::Pattern(self);
        }
        if !self.contains('\\') {
            return Segment::ExactMatch(Cow::from(self));
        }
        let mut result = String::with_capacity(self.len());
        let mut iter = self.chars();
        while let Some(c) = iter.next() {
            match c {
                '\\' => {
                    // push unescaped character since we are constructing an exact match.
                    if let Some(c) = iter.next() {
                        result.push(c);
                    }
                }
                c => result.push(c),
            }
        }
        Segment::ExactMatch(Cow::from(result))
    }
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

impl Into<fdiagnostics::StringSelector> for Segment<'_> {
    fn into(self) -> fdiagnostics::StringSelector {
        match self {
            Segment::ExactMatch(s) => fdiagnostics::StringSelector::ExactMatch(s.into_owned()),
            Segment::Pattern(s) => fdiagnostics::StringSelector::StringPattern(s.to_owned()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    fn convert_string_to_segment() {
        assert_eq!(Segment::ExactMatch(Cow::Borrowed("abc")), "abc".into());
        assert_eq!(Segment::Pattern("a*c"), "a*c".into());
        assert_eq!(Segment::ExactMatch(Cow::Owned("ac*".into())), "ac\\*".into());
        assert_eq!(Segment::Pattern("a\\*c*"), "a\\*c*".into());
    }
}
