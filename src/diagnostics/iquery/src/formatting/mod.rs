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
        PathFormat::Undefined | PathFormat::Full => location.to_string(),
    };
    if parts.is_empty() || (parts[0] == "root" && parts.len() == 1) {
        path
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
