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

use {crate::NodeHierarchy, anyhow::Error};

pub use crate::serialization::json::*;

pub mod json;
mod serialize;

/// Implementers of this trait will be able to convert an `Object` type data format that
/// is encoding a diagnostics data hierarchy into a NodeHierarchy.
pub trait HierarchyDeserializer<Key = String> {
    type Object;
    fn deserialize(data_format: Self::Object) -> Result<NodeHierarchy<Key>, Error>;
}
