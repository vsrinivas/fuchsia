// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        formatting::{format_parts, get_child_listing, get_locations, Formatter},
        options::PathFormat,
        result::IqueryResult,
    },
    failure::{bail, format_err, Error},
    inspect_formatter::{self, HierarchyFormatter},
    serde::ser::Serialize,
    serde_json::{
        json,
        ser::{PrettyFormatter, Serializer as JsonSerializer},
    },
    std::str::from_utf8,
};

pub struct JsonFormatter {
    path_format: PathFormat,
    max_depth: Option<u64>,
    sort: bool,
}

impl Formatter for JsonFormatter {
    fn new(path_format: PathFormat, max_depth: Option<u64>, sort: bool) -> Self {
        Self { path_format, max_depth, sort }
    }

    fn format_recursive(&self, results: Vec<IqueryResult>) -> Result<String, Error> {
        let hierachies = results
            .into_iter()
            .map(|mut result| {
                if self.sort {
                    result.sort_hierarchy();
                }
                if !result.is_loaded() {
                    bail!("Failed to format result at {}", result.location.to_string());
                }
                Ok(inspect_formatter::HierarchyData {
                    hierarchy: result.hierarchy.unwrap(),
                    file_path: match self.path_format {
                        PathFormat::Absolute => result.location.absolute_path_to_string()?,
                        PathFormat::Undefined | PathFormat::Full => result.location.to_string(),
                    },
                    fields: result.location.parts,
                })
            })
            .collect::<Result<Vec<inspect_formatter::HierarchyData>, Error>>()?;
        inspect_formatter::JsonFormatter::format_multiple(hierachies)
    }

    fn format_locations(&self, results: Vec<IqueryResult>) -> Result<String, Error> {
        let values = results
            .into_iter()
            .flat_map(|mut result| {
                if self.sort {
                    result.sort_hierarchy();
                }
                if self.max_depth.is_none() && result.is_loaded() {
                    get_locations(result.hierarchy.unwrap(), &result.location, &self.path_format)
                        .into_iter()
                } else {
                    vec![format_parts(&result.location, &self.path_format, &vec![])].into_iter()
                }
            })
            .collect();
        self.output(values)
    }

    fn format_child_listing(&self, results: Vec<IqueryResult>) -> Result<String, Error> {
        let children_names = get_child_listing(results, self.sort, &self.path_format);
        self.output(children_names)
    }
}

impl JsonFormatter {
    fn output<T: Serialize>(&self, values: Vec<T>) -> Result<String, Error> {
        let mut bytes = Vec::new();
        let mut serializer =
            JsonSerializer::with_formatter(&mut bytes, PrettyFormatter::with_indent(b"    "));
        json!(values).serialize(&mut serializer)?;
        from_utf8(&bytes)
            .map(|result| result.to_string())
            .map_err(|e| format_err!("Error serializing: {:?}", e))
    }
}
