// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_target_status_args::TargetStatus, std::io::Write};

const INDENT: usize = 4;

#[derive(Debug, PartialEq)]
pub enum StatusValue {
    BoolValue(bool),
    StringValue(String),
    StringListValue(Vec<String>),
}

impl std::fmt::Display for StatusValue {
    fn fmt<'a>(&self, f: &mut std::fmt::Formatter<'a>) -> std::fmt::Result {
        match self {
            StatusValue::BoolValue(value) => write!(f, "{:?}", value),
            StatusValue::StringValue(value) => write!(f, "{:?}", value),
            StatusValue::StringListValue(value) => write!(f, "{:?}", value),
        }
    }
}

#[derive(Debug, PartialEq)]
pub struct StatusEntry {
    pub title: String,
    pub label: String,
    pub description: String,
    pub value: Option<StatusValue>,
    pub child: Vec<StatusEntry>,
}

impl StatusEntry {
    pub fn new(title: &str, label: &str, description: &str) -> Self {
        Self {
            title: title.to_string(),
            label: label.to_string(),
            description: description.to_string(),
            value: None,
            child: vec![],
        }
    }

    // Create a Group StatusEntry.
    pub fn group(
        human_name: &str,
        id_name: &str,
        desc: &str,
        value: Vec<StatusEntry>,
    ) -> StatusEntry {
        let mut entry = StatusEntry::new(human_name, id_name, desc);
        entry.child = value;
        entry
    }

    // Create a Boolean StatusEntry.
    pub fn bool_value(
        human_name: &str,
        id_name: &str,
        desc: &str,
        value: &Option<bool>,
    ) -> StatusEntry {
        let mut entry = StatusEntry::new(human_name, id_name, desc);
        entry.value = value.map(|v| StatusValue::BoolValue(v));
        entry
    }

    // Create a string StatusEntry.
    pub fn str_value(
        human_name: &str,
        id_name: &str,
        desc: &str,
        value: &Option<String>,
    ) -> StatusEntry {
        let mut entry = StatusEntry::new(human_name, id_name, desc);
        entry.value = value.as_ref().map(|v| StatusValue::StringValue(v.to_string()));
        entry
    }
}

/// Write output for easy reading by humans.
fn output_list<W: Write>(
    indent: usize,
    statuses: &Vec<StatusEntry>,
    args: &TargetStatus,
    writer: &mut W,
) -> Result<()> {
    for status in statuses {
        let mut label = "".to_string();
        if args.label && !status.description.is_empty() {
            label = format!(" ({})", status.label);
        }
        let mut desc = "".to_string();
        if args.desc && !status.description.is_empty() {
            desc = format!(" # {}", status.description);
        }
        match &status.value {
            Some(value) => {
                writeln!(
                    *writer,
                    "{: <5$}{}{}: {}{}",
                    "", status.title, label, value, desc, indent
                )?;
            }
            None => writeln!(*writer, "{: <4$}{}{}: {}", "", status.title, label, desc, indent)?,
        }
        output_list(indent + INDENT, &status.child, args, writer)?;
    }
    Ok(())
}

/// Write output in English for easy reading by users.
pub fn output_for_human<W: Write>(
    statuses: &Vec<StatusEntry>,
    args: &TargetStatus,
    writer: &mut W,
) -> Result<()> {
    output_list(0, &statuses, args, writer)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_output_list() {
        let mut input = Vec::new();
        input.push(StatusEntry::new("Test", "the_test", "A test."));
        let mut result = Vec::new();
        output_list(7, &input, &TargetStatus { desc: false, label: false }, &mut result).unwrap();
        assert_eq!(result, b"       Test: \n");
    }

    #[test]
    fn test_output_list_with_child() {
        let mut input = Vec::new();
        input.push(StatusEntry::new("Test", "the_test", "A test."));
        input[0].child.push(StatusEntry::new("Prop", "a_prop", "Some data."));
        let mut result = Vec::new();
        output_list(0, &input, &TargetStatus { desc: false, label: false }, &mut result).unwrap();
        assert_eq!(result, b"Test: \n    Prop: \n");
    }

    #[test]
    fn test_output_for_human() {
        let mut input = Vec::new();
        input.push(StatusEntry::new("Test", "the_test", "A test."));
        input[0].child.push(StatusEntry::new("Prop", "a_prop", "Some data."));
        let mut result = Vec::new();
        output_for_human(&input, &TargetStatus { desc: false, label: false }, &mut result).unwrap();
        assert_eq!(result, b"Test: \n    Prop: \n");
    }
}
