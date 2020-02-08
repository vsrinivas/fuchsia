// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{location::InspectLocation, options::PathFormat, result::IqueryResult},
    anyhow::Error,
    fuchsia_inspect::reader::NodeHierarchy,
};

pub use crate::formatting::{json_formatter::JsonFormatter, text_formatter::TextFormatter};

mod json_formatter;
mod text_formatter;

pub trait Formatter {
    fn new(path_format: PathFormat, max_depth: Option<u64>, sort: bool) -> Self
    where
        Self: Sized;
    fn format_recursive(&self, results: Vec<IqueryResult>) -> Result<String, Error>;
    fn format_locations(&self, results: Vec<IqueryResult>) -> Result<String, Error>;
    fn format_child_listing(&self, results: Vec<IqueryResult>) -> Result<String, Error>;
}

pub fn format_parts(
    location: &InspectLocation,
    path_format: &PathFormat,
    parts: &[String],
) -> String {
    let path = match path_format {
        PathFormat::Absolute => location.absolute_path_to_string().unwrap(),
        PathFormat::Undefined | PathFormat::Full | PathFormat::Display => location.to_string(),
    };
    if parts.is_empty() || (parts[0] == "root" && parts.len() == 1) {
        path
    } else if *path_format == PathFormat::Display && parts.len() > 1 {
        parts[parts.len() - 1].clone()
    } else if parts[0] == "root" {
        format!("{}#{}", path, parts[1..].join("/"))
    } else {
        format!("{}#{}", path, parts.join("/"))
    }
}

fn get_child_listing(
    results: Vec<IqueryResult>,
    sort: bool,
    path_format: &PathFormat,
) -> Vec<String> {
    let mut children = vec![];
    for mut result in results {
        if !result.is_loaded() {
            continue;
        }
        if sort {
            result.sort_hierarchy();
        }
        let location = result.location.clone();
        if let Some(result_children) = get_children(result.hierarchy.unwrap(), &location.parts[..])
        {
            let mut parts = location.parts.clone();
            children.extend(result_children.into_iter().map(|child| {
                if *path_format == PathFormat::Undefined {
                    child.name
                } else {
                    parts.push(child.name);
                    let formatted_path = format_parts(&location, &path_format, &parts);
                    parts.pop();
                    formatted_path
                }
            }));
        }
    }
    children
}

fn get_children(hierarchy: NodeHierarchy, parts: &[String]) -> Option<Vec<NodeHierarchy>> {
    if parts.is_empty() {
        return Some(hierarchy.children);
    }
    hierarchy
        .children
        .into_iter()
        .find(|c| c.name == parts[0])
        .and_then(|child| get_children(child, &parts[1..]))
}

fn get_locations(
    root: NodeHierarchy,
    location: &InspectLocation,
    path_format: &PathFormat,
) -> Vec<String> {
    get_children_locations(root, location, path_format, &vec![])
}

fn get_children_locations(
    root: NodeHierarchy,
    location: &InspectLocation,
    path_format: &PathFormat,
    parts: &[String],
) -> Vec<String> {
    let mut path = parts.to_vec();
    path.push(root.name);
    let mut result = vec![format_parts(location, path_format, &path)];
    for child in root.children {
        result.extend(get_children_locations(child, location, path_format, &path))
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::location::InspectType;
    use std::path::PathBuf;

    /// Test for full or display output only.
    ///
    /// Absolute is not tested since it requires FS working directories to exist.
    /// Unknown is not tested since format_parts is not used for that purpose.
    struct TestCase {
        path: PathBuf,
        parts: &'static [&'static str],

        expected_full: &'static str,
        expected_display: &'static str,
    }

    fn do_test(case: TestCase) {
        let location =
            InspectLocation { inspect_type: InspectType::Vmo, path: case.path, parts: vec![] };

        let parts: Vec<String> = case.parts.into_iter().map(|x| x.to_string()).collect();
        let actual = (
            format_parts(&location, &PathFormat::Full, parts.as_slice()),
            format_parts(&location, &PathFormat::Display, parts.as_slice()),
        );

        assert_eq!((case.expected_full.to_string(), case.expected_display.to_string(),), actual);
    }

    #[test]
    fn test_path_formatting() {
        do_test(TestCase {
            path: PathBuf::from("123/root.inspect"),
            parts: &["root"],
            expected_full: "123/root.inspect",
            expected_display: "123/root.inspect",
        });

        do_test(TestCase {
            path: PathBuf::from("root.inspect"),
            parts: &["root", "test"],
            expected_full: "root.inspect#test",
            expected_display: "test",
        });

        do_test(TestCase {
            path: PathBuf::from("out/fuchsia.inspect.deprecated.Inspect"),
            parts: &["Name"],
            expected_full: "out#Name",
            expected_display: "out#Name",
        });

        do_test(TestCase {
            path: PathBuf::from("out/fuchsia.inspect.deprecated.Inspect"),
            parts: &["Name", "val"],
            expected_full: "out#Name/val",
            expected_display: "val",
        });

        do_test(TestCase {
            path: PathBuf::from("out/fuchsia.inspect.Tree"),
            parts: &["root"],
            expected_full: "out",
            expected_display: "out",
        });

        do_test(TestCase {
            path: PathBuf::from("out/fuchsia.inspect.Tree"),
            parts: &["root", "val"],
            expected_full: "out#val",
            expected_display: "val",
        });
    }
}
