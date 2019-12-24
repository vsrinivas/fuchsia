// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Inspect Format
//!
//! This module provides utilities for formatting a `NodeHierarchy`.
//! Currently the only available format is JSON.
//!
//! ## JSON Example
//!
//! If you'd like to format a single hierarchy
//!
//! ```
//! let hierarchy = NodeHierarchy::new(...);
//! let json_string = JsonNodeHierarchySerializer::serialize(hierarchy)
//! ```
//!
//! If you'd like to control more of your formatting than the inspect tree, you can do the
//! following:
//!
//! ```
//! let hierarchy = NodeHierarchy::new(...);
//! let json_value = RawJsonNodeHierarchySerializer::serialize(hierarchy)
//! // json!(json_value) or compose this value into your own JSON.
//! ```
//!
//! If you'd like to deserialize some json string, you can do the following:
//! ```
//! let string = "{ ... }".to_string();
//! let hierarchy = JsonNodeHierarchySerializer::deserialize(string)?;
//! ```

use {anyhow::Error, fuchsia_inspect::reader::NodeHierarchy};

pub use crate::deprecated_json::*;

pub mod deprecated_json;
pub mod json;
mod utils;

/// Implementers of this trait will provide different ways of formatting an
/// inspect hierarchy.
pub trait HierarchySerializer {
    type Type;
    fn serialize(hierarchy: NodeHierarchy) -> Self::Type;
}

/// Implementers of this trait will be able to convert an `Object` type data format that
/// is encoding a diagnostics data hierarchy into a NodeHierarchy.
pub trait HierarchyDeserializer {
    type Object;
    fn deserialize(data_format: Self::Object) -> Result<NodeHierarchy, Error>;
}

/// Node hierarchies to be formatted including information about the path.
/// Example:
///
/// ```
/// HierarchyData {
///     hierarchy: SOME_HIERARCHY,
///     file_path: "/some/path",
///     fields: vec!["root", "node1", "node2"],
/// }
/// ```
///
/// Means that the hierarchy `SOME_HIERARCHY` points to the inspect node named
/// `node2` that is child of the node `node1` under `root` in an inspect file
/// located at `/some/path`.
///
pub struct HierarchyData {
    /// The node hierarchy to be formatted.
    pub hierarchy: NodeHierarchy,

    /// The path where the inspect file is.
    pub file_path: String,

    /// The path to the node within the inspect tree.
    pub fields: Vec<String>,
}

/// Implementers of this trait will provide different ways of formatting an
/// inspect hierarchy.
pub trait DeprecatedHierarchyFormatter {
    fn format(hierarchy: HierarchyData) -> Result<String, Error>;
    fn format_multiple(hierarchies: Vec<HierarchyData>) -> Result<String, Error>;
}
