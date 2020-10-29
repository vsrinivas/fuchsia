// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_target_status_args::TargetStatus, serde::Serialize, std::io::Write};

/// Store, organize, and display hierarchical status information. Output may be
/// formatted for a human reader or structured as JSON for machine consumption.

const INDENT: usize = 4;

/// Status entry values.
#[derive(Debug, PartialEq, Serialize)]
#[serde(untagged)]
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

/// A node in a hierarchy of status information or groupings.
#[derive(Debug, PartialEq, Serialize)]
pub struct StatusEntry {
    pub title: String,
    pub label: String,
    pub description: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub value: Option<StatusValue>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
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
        if args.label && !status.label.is_empty() {
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

/// Write output in JSON for easy parsing by other tools.
pub fn output_for_machine<W: Write>(
    statuses: &Vec<StatusEntry>,
    _args: &TargetStatus,
    writer: &mut W,
) -> Result<()> {
    Ok(write!(writer, "{}", serde_json::to_string(&statuses)?)?)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_display_output() {
        assert_eq!(format!("{}", StatusValue::BoolValue(false)), "false");
        assert_eq!(format!("{}", StatusValue::BoolValue(true)), "true");
        assert_eq!(format!("{}", StatusValue::StringValue("abc".to_string())), "\"abc\"");
        assert_eq!(format!("{}", StatusValue::StringValue("ab\"c".to_string())), "\"ab\\\"c\"");
        assert_eq!(
            format!("{}", StatusValue::StringListValue(vec!["abc".to_string(), "def".to_string()])),
            "[\"abc\", \"def\"]"
        );
    }

    #[test]
    fn test_output_list() {
        let input = vec![StatusEntry::new("Test", "the_test", "A test.")];
        let mut result = Vec::new();
        output_list(
            7,
            &input,
            &TargetStatus { desc: false, label: false, json: false },
            &mut result,
        )
        .unwrap();
        assert_eq!(result, b"       Test: \n");
    }

    #[test]
    fn test_output_list_with_child() {
        let input = vec![StatusEntry::group(
            "Test",
            "the_test",
            "A test.",
            vec![StatusEntry::new("Prop", "a_prop", "Some data.")],
        )];
        let mut result = Vec::new();
        output_list(
            0,
            &input,
            &TargetStatus { desc: false, label: false, json: false },
            &mut result,
        )
        .unwrap();
        assert_eq!(result, b"Test: \n    Prop: \n");
    }

    #[test]
    fn test_output_for_human() {
        let input = vec![StatusEntry::group(
            "Test",
            "the_test",
            "A test.",
            vec![StatusEntry::bool_value("Prop", "a_prop", "Some data.", &Some(false))],
        )];
        let mut result = Vec::new();
        output_for_human(
            &input,
            &TargetStatus { desc: false, label: false, json: false },
            &mut result,
        )
        .unwrap();
        assert_eq!(result, b"Test: \n    Prop: false\n");
    }

    #[test]
    fn test_output_for_machine() {
        let input = vec![StatusEntry::group(
            "Test",
            "the_test",
            "A test.",
            vec![StatusEntry::bool_value("Prop", "a_prop", "Some data.", &Some(false))],
        )];
        let mut result = Vec::new();
        output_for_machine(
            &input,
            &TargetStatus { desc: false, label: false, json: true },
            &mut result,
        )
        .unwrap();
        assert_eq!(
            result,
            b"[{\"title\":\"Test\",\"label\":\"the_test\",\"description\"\
                             :\"A test.\",\"child\":[{\"title\":\"Prop\",\"label\":\"a_prop\",\
                             \"description\":\"Some data.\",\"value\":false}]}]"
        );
    }
}
