// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        deprecated_options::PathFormat,
        formatting::{format_parts, get_child_listing, get_locations, Formatter},
        location::InspectLocation,
        result::IqueryResult,
    },
    anyhow::{format_err, Error},
    fuchsia_inspect::reader::{ArrayContent, NodeHierarchy, Property},
    nom::HexDisplay,
    num_traits::Bounded,
    std::{
        fmt,
        ops::{Add, AddAssign, MulAssign},
    },
};

const INDENT: usize = 2;
const HEX_DISPLAY_CHUNK_SIZE: usize = 16;

pub struct TextFormatter {
    path_format: PathFormat,
    max_depth: Option<u64>,
    sort: bool,
}

impl Formatter for TextFormatter {
    fn new(path_format: PathFormat, max_depth: Option<u64>, sort: bool) -> Self {
        Self { path_format, max_depth, sort }
    }

    fn format_recursive(&self, results: Vec<IqueryResult>) -> Result<String, Error> {
        let mut outputs = vec![];
        for result in results.into_iter() {
            let mut hierarchy = match result.get_query_hierarchy() {
                None => return Err(format_err!("No node hierachy found")),
                Some(h) => h.clone(),
            };
            if self.max_depth.is_none() {
                hierarchy.children = vec![];
            }
            if self.sort {
                hierarchy.sort();
            }
            let path = result.location.query_path();
            outputs.push(self.output_hierarchy_deprecated(hierarchy, &result.location, &path, 0));
        }
        Ok(outputs.join("\n"))
    }

    fn format_locations(&self, results: Vec<IqueryResult>) -> Result<String, Error> {
        let mut outputs = vec![];
        for mut result in results.into_iter() {
            if self.sort {
                result.sort_hierarchy();
            }
            if self.max_depth.is_none() && result.is_loaded() {
                let locations =
                    get_locations(result.hierarchy.unwrap(), &result.location, &self.path_format);
                outputs.extend(locations.into_iter().map(|location| format!("{}", location)));
            } else {
                outputs
                    .push(format!("{}", format_parts(&result.location, &self.path_format, &vec![])))
            }
        }
        Ok(outputs.join("\n"))
    }

    fn format_child_listing(&self, results: Vec<IqueryResult>) -> Result<String, Error> {
        Ok(get_child_listing(results, self.sort, &self.path_format).join("\n"))
    }
}

// TODO(fxbug.dev/45458): clean this up once the deprecated flags are removed.
pub fn format(path: &str, node_hierarchy: NodeHierarchy) -> String {
    let formatter = TextFormatter { path_format: PathFormat::Display, max_depth: None, sort: true };
    let result = formatter.output_hierarchy(node_hierarchy, 1);
    format!("{}:\n{}", path, result)
}

impl TextFormatter {
    fn output_hierarchy(&self, node_hierarchy: NodeHierarchy, indent: usize) -> String {
        let mut lines = vec![];
        let name_indent = " ".repeat(INDENT * indent);
        let value_indent = " ".repeat(INDENT * (indent + 1));

        lines.push(format!("{}{}:", name_indent, node_hierarchy.name));

        lines.extend(node_hierarchy.properties.into_iter().map(|property| match property {
            Property::String(name, value) => format!("{}{} = {}", value_indent, name, value),
            Property::Int(name, value) => format!("{}{} = {}", value_indent, name, value),
            Property::Uint(name, value) => format!("{}{} = {}", value_indent, name, value),
            Property::Double(name, value) => format!("{}{} = {:.6}", value_indent, name, value),
            Property::Bytes(name, array) => {
                let byte_str = array.to_hex(HEX_DISPLAY_CHUNK_SIZE);
                format!("{}{} = Binary:\n{}", value_indent, name, byte_str.trim())
            }
            Property::Bool(name, value) => format!("{}{} = {}", value_indent, name, value),
            Property::IntArray(name, array) => self.output_array(&value_indent, &name, &array),
            Property::UintArray(name, array) => self.output_array(&value_indent, &name, &array),
            Property::DoubleArray(name, array) => self.output_array(&value_indent, &name, &array),
        }));

        lines.extend(
            node_hierarchy
                .children
                .into_iter()
                .map(|child| self.output_hierarchy(child, indent + 1)),
        );

        lines.join("\n")
    }

    fn output_hierarchy_deprecated(
        &self,
        node_hierarchy: NodeHierarchy,
        location: &InspectLocation,
        path: &[String],
        indent: usize,
    ) -> String {
        let mut lines = vec![];
        let name_indent = " ".repeat(INDENT * indent);
        let value_indent = " ".repeat(INDENT * (indent + 1));

        lines.push(self.output_name_deprecated(
            &name_indent,
            &path,
            &node_hierarchy.name,
            location,
        ));

        lines.extend(node_hierarchy.properties.into_iter().map(|property| match property {
            Property::String(name, value) => format!("{}{} = {}", value_indent, name, value),
            Property::Int(name, value) => format!("{}{} = {}", value_indent, name, value),
            Property::Uint(name, value) => format!("{}{} = {}", value_indent, name, value),
            Property::Double(name, value) => format!("{}{} = {:.6}", value_indent, name, value),
            Property::Bytes(name, array) => {
                let byte_str = array.to_hex(HEX_DISPLAY_CHUNK_SIZE);
                format!("{}{} = Binary:\n{}", value_indent, name, byte_str.trim())
            }
            Property::Bool(name, value) => format!("{}{} = {}", value_indent, name, value),
            Property::IntArray(name, array) => self.output_array(&value_indent, &name, &array),
            Property::UintArray(name, array) => self.output_array(&value_indent, &name, &array),
            Property::DoubleArray(name, array) => self.output_array(&value_indent, &name, &array),
        }));

        let mut child_path = path.to_vec();
        child_path.push(node_hierarchy.name);
        lines.extend(node_hierarchy.children.into_iter().map(|child| {
            self.output_hierarchy_deprecated(child, location, &child_path, indent + 1)
        }));

        lines.join("\n")
    }

    fn output_name_deprecated(
        &self,
        name_indent: &str,
        path: &[String],
        name: &str,
        location: &InspectLocation,
    ) -> String {
        let output = match self.path_format {
            PathFormat::Undefined => name.to_string(),
            _ => {
                let mut parts = path.to_vec();
                parts.push(name.to_string());
                format_parts(location, &self.path_format, &parts)
            }
        };
        format!("{}{}:", name_indent, output)
    }

    fn output_array<
        T: AddAssign + MulAssign + Copy + Add<Output = T> + fmt::Display + NumberFormat + Bounded,
    >(
        &self,
        value_indent: &str,
        name: &str,
        array: &ArrayContent<T>,
    ) -> String {
        let content = match array {
            ArrayContent::Values(values) => {
                values.iter().map(|x| x.to_string()).collect::<Vec<String>>()
            }
            ArrayContent::Buckets(buckets) => buckets
                .iter()
                .map(|bucket| {
                    format!(
                        "[{},{})={}",
                        bucket.floor.format(),
                        bucket.upper.format(),
                        bucket.count.format()
                    )
                })
                .collect::<Vec<String>>(),
        };
        format!("{}{} = [{}]", value_indent, name, content.join(", "))
    }
}

trait NumberFormat {
    fn format(&self) -> String;
}

impl NumberFormat for i64 {
    fn format(&self) -> String {
        match *self {
            std::i64::MAX => "<max>".to_string(),
            std::i64::MIN => "<min>".to_string(),
            x => format!("{}", x),
        }
    }
}

impl NumberFormat for u64 {
    fn format(&self) -> String {
        match *self {
            std::u64::MAX => "<max>".to_string(),
            x => format!("{}", x),
        }
    }
}

impl NumberFormat for f64 {
    fn format(&self) -> String {
        if *self == std::f64::MAX || *self == std::f64::INFINITY {
            "inf".to_string()
        } else if *self == std::f64::MIN || *self == std::f64::NEG_INFINITY {
            "-inf".to_string()
        } else {
            format!("{}", self)
        }
    }
}
