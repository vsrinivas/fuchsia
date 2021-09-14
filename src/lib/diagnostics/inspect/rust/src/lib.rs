// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # fuchsia-inspect
//!
//! Components in Fuchsia may expose structured information about themselves conforming to the
//! [Inspect API][inspect]. This crate is the core library for writing inspect data in Rust
//! components.
//!
//! For a comprehensive guide on how to start using inspect, please refer to the
//! [codelab].
//!
//! ## Library concepts
//!
//! There's two types of inspect values: nodes and properties. These have the following
//! characteristics:
//!
//!   - A Node may have any number of key/value pairs called Properties.
//!   - A Node may have any number of children, which are also Nodes.
//!   - Properties and nodes are created under a parent node. Inspect is already initialized with a
//!     root node.
//!   - The key for a value in a Node is always a UTF-8 string, the value may be one of the
//!     supported types (a node or a property of any type).
//!   - Nodes and properties have strict ownership semantics. Whenever a node or property is
//!     created, it is written to the backing [VMO][inspect-vmo] and whenever it is dropped it is
//!     removed from the VMO.
//!   - Inspection is best effort, if an error occurs, no panic will happen and nodes and properties
//!     might become No-Ops. For example, when the VMO becomes full, any further creation of a
//!     property or a node will result in no changes in the VMO and a silent failure. However,
//!     mutation of existing properties in the VMO will continue to work.
//!   - All nodes and properties are thread safe.
//!
//! ### Creating vs Recording
//!
//! There are two functions each for initializing nodes and properties:
//!
//!   - `create_*`: returns the created node/property and it's up to the caller to handle its
//!     lifetime.
//!   - `record_*`: creates the node/property but doesn't return it and ties its lifetime to
//!     the node where the function was called.
//!
//! ### Lazy value support
//!
//! Lazy (or dynamic) values are values that are created on demand, this is, whenever they are read.
//! Unlike regular nodes, they don't take any space on the VMO until a reader comes and requests
//! its data.
//!
//! There's two ways of creating lazy values:
//!
//!   - **Lazy node**: creates a child node of root with the given name. The callback returns a
//!     future for an [`Inspector`][inspector] whose root node is spliced into the parent node when
//!     read.
//!   - **Lazy values**: works like the previous one, except that all properties and nodes under the
//!     future root node node are added directly as children of the parent node.
//!
//! ## Quickstart
//!
//! Add the following to your component main:
//!
//! ```rust
//! use fuchsia_inspect::component;
//! use fuchsia_component::server::ServiceFs;
//! use inspect_runtime;
//!
//! let mut fs = ServiceFs::new();
//! inspect_runtime::serve(component::inspector(), &mut fs)?;
//!
//! // Now you can create nodes and properties anywhere!
//! let child = component::inspector().root().create_child("foo");
//! child.record_uint("bar", 42);
//! ```
//!
//! [inspect]: https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect
//! [codelab]: https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect/codelab
//! [inspect-vmo]: https://fuchsia.dev/fuchsia-src/reference/diagnostics/inspect/vmo-format
//! [inspector]: Inspector

pub mod component;
pub mod health;
pub mod reader;
pub mod stats;
mod writer;

pub use diagnostics_hierarchy::{
    DiagnosticsHierarchy, ExponentialHistogramParams, LinearHistogramParams,
};

pub use {crate::state::Stats, crate::writer::*};

pub use {testing::*, writer::types::*};

#[doc(hidden)]
pub use writer::heap;

pub mod testing {
    pub use diagnostics_hierarchy::{
        assert_data_tree,
        testing::{
            AnyProperty, DiagnosticsHierarchyGetter, HistogramAssertion, NonZeroUintProperty,
            PropertyAssertion, TreeAssertion,
        },
        tree_assertion,
    };
}

/// Directiory within the outgoing directory of a component where the diagnostics service should be
/// added.
pub const DIAGNOSTICS_DIR: &str = "diagnostics";
