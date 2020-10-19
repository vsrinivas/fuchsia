// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The `inspect-writable` library defines traits for simple data structures that can be
//! written to inspect and exports a procedural macro to implement these traits.

// TODO(jsankey): More documentation and examples when this is stable, and try to move it
// somewhere into inspect contrib.

// Re-export the derive-macro.
pub use inspect_writable_derive::*;

use fuchsia_inspect::Node;

/// A datatype that may be written to an inspect type. This trait can be automatically derived for
/// structs composed of inspect-compatible fields, i.e. signed integers, unsigned integers, or types
/// implementing the `Debug` trait.
pub trait InspectWritable: Sized {
    /// The wrapper type returned from `create` calls.
    type NodeType: InspectWritableNode<Self>;

    /// Writes the contents of the struct into inspect fields on the supplied node. Field names
    /// match the fields in the struct. This function uses the `create*` methods in the inspect
    /// API and returns an `InspectWriteableNode` that may be used to update the fields in the
    /// future.
    fn create(&self, node: Node) -> Self::NodeType;

    /// Writes the contents of the struct into inspect fields on the supplied node. Field names
    /// match the fields in the struct. This function uses the `record*` methods in the inspect
    /// API and should be used for fields that are never modified.
    fn record(&self, node: &Node);
}

/// A wrapper around an Inspect node and a collection of inspect fields. These fields are created
/// and may updated using the contents of an `InspectWritable` struct. A struct implementing
/// `InspectWritableNode` is automatically generated when the `InspectWritable` trait is derived.
pub trait InspectWritableNode<T: InspectWritable> {
    /// Create a new instance, wrapping the supplied `node`. Inspect fields are created with names
    /// matching those in the `InspectWritable` struct and initialized to the values in `data`.
    fn new(data: &T, node: Node) -> Self;

    /// Updates all inspect fields to the values in `data`.
    fn update(&self, data: &T);

    /// Returns a reference to the wrapped inspect `Node`.
    fn inspect_node(&self) -> &Node;
}
