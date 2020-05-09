// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use regex::Regex;
use std::fmt;

/// A segement of a label specification.
#[derive(PartialEq, Eq, PartialOrd, Ord, Clone)]
pub enum LabelSpecPart<'a> {
    Binding,
    BenchmarkCase,
    Measurement,
    Literal(&'a str),
    Any,
}

impl fmt::Display for LabelSpecPart<'_> {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt.write_str(match self {
            LabelSpecPart::Binding => "Binding",
            LabelSpecPart::BenchmarkCase => "BenchmarkCase",
            LabelSpecPart::Measurement => "Measurement",
            LabelSpecPart::Literal(v) => v,
            LabelSpecPart::Any => "Any",
        })
    }
}

/// Specification of the structure of a BenchmarkResult label string.
///
/// Each string label an be broken into parts, e.g.:
/// "[Binding]/Encode/[BenchmarkCase]/[Measurement]"
/// This specifies that structure.
pub struct LabelSpec<'a> {
    parts: Vec<LabelSpecPart<'a>>,
}

impl LabelSpec<'_> {
    pub fn new<'a>(parts: Vec<LabelSpecPart<'a>>) -> LabelSpec<'a> {
        // Don't allow duplicate parts in the definition.
        let mut deduped_parts = parts.to_vec();
        deduped_parts.sort_unstable();
        deduped_parts.dedup();
        assert_eq!(deduped_parts.len(), parts.len());

        LabelSpec { parts }
    }

    fn regex_part<'a>(part: &LabelSpecPart<'a>) -> String {
        match part {
            LabelSpecPart::Binding => "(LLCPP|HLCPP|Go|Rust|Dart)".to_string(),
            LabelSpecPart::BenchmarkCase => "([A-Za-z0-9/]+)".to_string(),
            LabelSpecPart::Measurement => {
                "(WallTime|Allocations|AllocatedBytes|StackDepth)".to_string()
            }
            LabelSpecPart::Literal(v) => regex::escape(v),
            LabelSpecPart::Any => "(.*)".to_string(),
        }
    }

    pub fn parse<'a>(&'a self, s: &'a str) -> Option<ParsedLabelValues<'a>> {
        let v: Vec<String> = self.parts.iter().map(Self::regex_part).collect();
        let regex_str = format!("^{}$", &v.join("/"));
        let regex = Regex::new(&regex_str).unwrap();
        let captures = regex.captures(s)?;
        let mut capture_iter = captures.iter().skip(1);
        let mut result_vec: Vec<ParsedLabelValue<'a>> = Vec::new();
        for part in self.parts.iter() {
            match part {
                LabelSpecPart::Literal(_) => (),
                _ => {
                    let capture = capture_iter.next()?;
                    result_vec.push(ParsedLabelValue { part: &part, text: capture?.as_str() })
                }
            }
        }
        if capture_iter.next().is_none() {
            Some(ParsedLabelValues { parts: result_vec })
        } else {
            None
        }
    }

    pub fn build_str<'a>(&'a self, values: &ParsedLabelValues<'a>) -> Option<String> {
        let mut str_parts: Vec<&str> = Vec::new();
        for part in self.parts.iter() {
            match part {
                LabelSpecPart::Literal(v) => {
                    str_parts.push(v);
                }
                _ => {
                    str_parts.push(values.parts.iter().find(|val| val.part == part)?.text);
                }
            }
        }
        Some(str_parts.join("/"))
    }
}

/// Specification for a transformation in labels.
///
/// The transformation is from the format specified in the "from" spec
/// to the "to" spec.
pub struct LabelTransformSpec<'a> {
    pub from: LabelSpec<'a>,
    pub to: LabelSpec<'a>,
}

impl LabelTransformSpec<'_> {
    pub fn transform(&self, from_label: &str) -> Option<String> {
        self.to.build_str(&self.from.parse(from_label)?)
    }
}

/// Representation of a piece of a parsed label.
pub struct ParsedLabelValue<'a> {
    part: &'a LabelSpecPart<'a>,
    text: &'a str,
}

/// Representation of a parsed label.
pub struct ParsedLabelValues<'a> {
    parts: Vec<ParsedLabelValue<'a>>,
}

#[cfg(test)]
mod tests {
    use super::LabelSpecPart::*;
    use super::*;

    #[test]
    fn test_label_transform_spec() {
        let from_spec =
            LabelSpec::new(vec![Binding, BenchmarkCase, Measurement, Literal("abc"), Any]);
        let to_spec =
            LabelSpec::new(vec![Literal("def"), BenchmarkCase, Measurement, Binding, Any]);
        let label_transform_spec = LabelTransformSpec { from: from_spec, to: to_spec };

        let result = label_transform_spec.transform("Rust/MyTest/Len1/WallTime/abc/something/here");
        assert_eq!(result, Some("def/MyTest/Len1/WallTime/Rust/something/here".to_string()));
    }
}
