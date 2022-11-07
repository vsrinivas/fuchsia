// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![deny(missing_docs)]
use {std::collections::HashMap, std::collections::HashSet, std::hash::Hash, std::hash::Hasher};

/// Options that can be applied to specific objects or arrays in the target JSON5 schema, through
/// [FormatOptions.options_by_path](struct.FormatOptions.html#structfield.options_by_path).
/// Each option can be set at most once per unique path.
#[derive(Clone, Debug)]
pub enum PathOption {
    /// For matched paths, overrides the FormatOption.trailing_comma provided default.
    TrailingCommas(bool),

    /// For matched paths, overrides the FormatOption.collapse_container_of_one provided default.
    CollapseContainersOfOne(bool),

    /// For matched paths, overrides the FormatOption.sort_array_items provided default.
    SortArrayItems(bool),

    /// Contains a vector of property names. When formatting an object matching the path in
    /// `FormatOptions.options_by_path` a specified path, properties of the object will be sorted
    /// to match the given order. Any properties not in this list will retain their original order,
    /// and placed after the sorted properties.
    PropertyNameOrder(Vec<&'static str>),
}

impl PartialEq for PathOption {
    fn eq(&self, other: &Self) -> bool {
        use PathOption::*;
        match (self, other) {
            (&TrailingCommas(..), &TrailingCommas(..)) => true,
            (&CollapseContainersOfOne(..), &CollapseContainersOfOne(..)) => true,
            (&SortArrayItems(..), &SortArrayItems(..)) => true,
            (&PropertyNameOrder(..), &PropertyNameOrder(..)) => true,
            _ => false,
        }
    }
}

impl Eq for PathOption {}

impl Hash for PathOption {
    fn hash<H: Hasher>(&self, state: &mut H) {
        use PathOption::*;
        state.write_u32(match self {
            TrailingCommas(..) => 1,
            CollapseContainersOfOne(..) => 2,
            SortArrayItems(..) => 3,
            PropertyNameOrder(..) => 4,
        });
        state.finish();
    }
}

/// Options that change the style of the formatted JSON5 output.
#[derive(Clone, Debug)]
pub struct FormatOptions {
    /// Indent the content of an object or array by this many spaces.
    pub indent_by: usize,

    /// Add a trailing comma after the last element in an array or object.
    pub trailing_commas: bool,

    /// If an array or object has only one item (or is empty), and no internal comments, collapse
    /// the array or object to a single line.
    pub collapse_containers_of_one: bool,

    /// If true, sort array primitive values lexicographically. Be aware that the order may not
    /// matter in some use cases, but can be very important in others. Consider setting this
    /// option for specific property paths only, and otherwise use the default (false).
    pub sort_array_items: bool,

    /// A set of "paths", to identify elements of the JSON structure, mapped to a set of one or
    /// more [PathOption](enum.PathOption.html) settings.
    pub options_by_path: HashMap<&'static str, HashSet<PathOption>>,
}

impl Default for FormatOptions {
    fn default() -> Self {
        FormatOptions {
            indent_by: 4,
            trailing_commas: true,
            collapse_containers_of_one: false,
            sort_array_items: false,
            options_by_path: HashMap::new(),
        }
    }
}
