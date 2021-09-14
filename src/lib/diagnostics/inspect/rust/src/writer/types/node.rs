// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::writer::{
    BoolProperty, BytesProperty, DoubleArrayProperty, DoubleExponentialHistogramProperty,
    DoubleLinearHistogramProperty, DoubleProperty, Error, Inner, InnerType, InspectType,
    InspectTypeInternal, Inspector, IntArrayProperty, IntExponentialHistogramProperty,
    IntLinearHistogramProperty, IntProperty, LazyNode, State, StringProperty, StringReference,
    UintArrayProperty, UintExponentialHistogramProperty, UintLinearHistogramProperty, UintProperty,
    ValueList,
};
use diagnostics_hierarchy::{
    ArrayFormat, ExponentialHistogramParams, LinearHistogramParams, LinkNodeDisposition,
};
use fuchsia_zircon as zx;
use futures::{future::BoxFuture, FutureExt};
use inspect_format::PropertyFormat;
use std::sync::Arc;

#[cfg(test)]
use {inspect_format::Block, mapped_vmo::Mapping};

/// Inspect Node data type.
///
/// NOTE: do not rely on PartialEq implementation for true comparison.
/// Instead leverage the reader.
///
/// NOTE: Operations on a Default value are no-ops.
#[derive(Debug, PartialEq, Eq, Default)]
pub struct Node {
    pub(crate) inner: Inner<InnerNodeType>,
}

impl InspectType for Node {}

impl InspectTypeInternal for Node {
    fn new(state: State, block_index: u32) -> Self {
        Self { inner: Inner::new(state, block_index) }
    }

    fn is_valid(&self) -> bool {
        self.inner.is_valid()
    }

    fn new_no_op() -> Self {
        Self { inner: Inner::None }
    }
}

impl Node {
    /// Create a weak reference to the original node. All operations on a weak
    /// reference have identical semantics to the original node for as long
    /// as the original node is live. After that, all operations are no-ops.
    pub fn clone_weak(&self) -> Node {
        Self { inner: self.inner.clone_weak() }
    }

    /// Add a child to this node.
    #[must_use]
    pub fn create_child<'b>(&self, name: impl Into<StringReference<'b>>) -> Node {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| state.create_node(name, inner_ref.block_index))
                    .map(|block| Node::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(Node::new_no_op())
    }

    /// Creates and keeps track of a child with the given `name`.
    pub fn record_child<'b, F>(&self, name: impl Into<StringReference<'b>>, initialize: F)
    where
        F: FnOnce(&Node),
    {
        let child = self.create_child(name);
        initialize(&child);
        self.record(child);
    }

    /// Takes a function to execute as under a single lock of the Inspect VMO. This function
    /// receives a reference to the `Node` where this is called.
    pub fn atomic_update<F, R>(&self, mut update_fn: F) -> R
    where
        F: FnMut(&Node) -> R,
    {
        match self.inner.inner_ref() {
            None => {
                // If the node was a no-op we still execute the `update_fn` even if all operations
                // inside it will be no-ops to return `R`.
                update_fn(&self)
            }
            Some(inner_ref) => {
                // Silently ignore the error when fail to lock (as in any regular operation).
                // All operations performed in the `update_fn` won't update the vmo
                // generation count since we'll be holding one lock here.
                inner_ref.state.begin_transaction();
                let result = update_fn(&self);
                inner_ref.state.end_transaction();
                result
            }
        }
    }

    /// Keeps track of the given property for the lifetime of the node.
    pub fn record(&self, property: impl InspectType + 'static) {
        self.inner.inner_ref().map(|inner_ref| inner_ref.data.record(property));
    }

    /// Creates a new `IntProperty` with the given `name` and `value`.
    #[must_use]
    pub fn create_int<'b>(&self, name: impl Into<StringReference<'b>>, value: i64) -> IntProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_int_metric(name, value, inner_ref.block_index)
                    })
                    .map(|block| IntProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(IntProperty::new_no_op())
    }

    /// Records a new `IntProperty` with the given `name` and `value`.
    pub fn record_int<'b>(&self, name: impl Into<StringReference<'b>>, value: i64) {
        let property = self.create_int(name, value);
        self.record(property);
    }

    /// Creates a new `UintProperty` with the given `name` and `value`.
    #[must_use]
    pub fn create_uint<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        value: u64,
    ) -> UintProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_uint_metric(name, value, inner_ref.block_index)
                    })
                    .map(|block| UintProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(UintProperty::new_no_op())
    }

    /// Records a new `UintProperty` with the given `name` and `value`.
    pub fn record_uint<'b>(&self, name: impl Into<StringReference<'b>>, value: u64) {
        let property = self.create_uint(name, value);
        self.record(property);
    }

    /// Creates a new `DoubleProperty` with the given `name` and `value`.
    #[must_use]
    pub fn create_double<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        value: f64,
    ) -> DoubleProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_double_metric(name, value, inner_ref.block_index)
                    })
                    .map(|block| DoubleProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(DoubleProperty::new_no_op())
    }

    /// Records a new `DoubleProperty` with the given `name` and `value`.
    pub fn record_double<'b>(&self, name: impl Into<StringReference<'b>>, value: f64) {
        let property = self.create_double(name, value);
        self.record(property);
    }

    /// Creates a new `IntArrayProperty` with the given `name` and `slots`.
    #[must_use]
    pub fn create_int_array<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        slots: usize,
    ) -> IntArrayProperty {
        self.create_int_array_internal(name, slots, ArrayFormat::Default)
    }

    #[must_use]
    pub(crate) fn create_int_array_internal<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        slots: usize,
        format: ArrayFormat,
    ) -> IntArrayProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_int_array(name, slots, format, inner_ref.block_index)
                    })
                    .map(|block| IntArrayProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(IntArrayProperty::new_no_op())
    }

    /// Creates a new `UintArrayProperty` with the given `name` and `slots`.
    #[must_use]
    pub fn create_uint_array<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        slots: usize,
    ) -> UintArrayProperty {
        self.create_uint_array_internal(name, slots, ArrayFormat::Default)
    }

    #[must_use]
    pub(crate) fn create_uint_array_internal<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        slots: usize,
        format: ArrayFormat,
    ) -> UintArrayProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_uint_array(name, slots, format, inner_ref.block_index)
                    })
                    .map(|block| UintArrayProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(UintArrayProperty::new_no_op())
    }

    /// Creates a new `DoubleArrayProperty` with the given `name` and `slots`.
    #[must_use]
    pub fn create_double_array<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        slots: usize,
    ) -> DoubleArrayProperty {
        self.create_double_array_internal(name, slots, ArrayFormat::Default)
    }

    #[must_use]
    pub(crate) fn create_double_array_internal<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        slots: usize,
        format: ArrayFormat,
    ) -> DoubleArrayProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_double_array(name, slots, format, inner_ref.block_index)
                    })
                    .map(|block| DoubleArrayProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(DoubleArrayProperty::new_no_op())
    }

    /// Creates a new `IntLinearHistogramProperty` with the given `name` and `params`.
    #[must_use]
    pub fn create_int_linear_histogram<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        params: LinearHistogramParams<i64>,
    ) -> IntLinearHistogramProperty {
        IntLinearHistogramProperty::new(name, params, &self)
    }

    /// Creates a new `UintLinearHistogramProperty` with the given `name` and `params`.
    #[must_use]
    pub fn create_uint_linear_histogram<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        params: LinearHistogramParams<u64>,
    ) -> UintLinearHistogramProperty {
        UintLinearHistogramProperty::new(name, params, &self)
    }

    /// Creates a new `DoubleLinearHistogramProperty` with the given `name` and `params`.
    #[must_use]
    pub fn create_double_linear_histogram<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        params: LinearHistogramParams<f64>,
    ) -> DoubleLinearHistogramProperty {
        DoubleLinearHistogramProperty::new(name, params, &self)
    }

    /// Creates a new `IntExponentialHistogramProperty` with the given `name` and `params`.
    #[must_use]
    pub fn create_int_exponential_histogram<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        params: ExponentialHistogramParams<i64>,
    ) -> IntExponentialHistogramProperty {
        IntExponentialHistogramProperty::new(name, params, &self)
    }

    /// Creates a new `UintExponentialHistogramProperty` with the given `name` and `params`.
    #[must_use]
    pub fn create_uint_exponential_histogram<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        params: ExponentialHistogramParams<u64>,
    ) -> UintExponentialHistogramProperty {
        UintExponentialHistogramProperty::new(name, params, &self)
    }

    /// Creates a new `DoubleExponentialHistogramProperty` with the given `name` and `params`.
    #[must_use]
    pub fn create_double_exponential_histogram<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        params: ExponentialHistogramParams<f64>,
    ) -> DoubleExponentialHistogramProperty {
        DoubleExponentialHistogramProperty::new(name, params, &self)
    }

    /// Creates a new lazy child with the given `name` and `callback`.
    #[must_use]
    pub fn create_lazy_child<'b, F>(
        &self,
        name: impl Into<StringReference<'b>>,
        callback: F,
    ) -> LazyNode
    where
        F: Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send + 'static,
    {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_lazy_node(
                            name,
                            inner_ref.block_index,
                            LinkNodeDisposition::Child,
                            callback,
                        )
                    })
                    .map(|block| LazyNode::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(LazyNode::new_no_op())
    }

    /// Records a new lazy child with the given `name` and `callback`.
    pub fn record_lazy_child<'b, F>(&self, name: impl Into<StringReference<'b>>, callback: F)
    where
        F: Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send + 'static,
    {
        let property = self.create_lazy_child(name, callback);
        self.record(property);
    }

    /// Creates a new inline lazy node with the given `name` and `callback`.
    #[must_use]
    pub fn create_lazy_values<'b, F>(
        &self,
        name: impl Into<StringReference<'b>>,
        callback: F,
    ) -> LazyNode
    where
        F: Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send + 'static,
    {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_lazy_node(
                            name,
                            inner_ref.block_index,
                            LinkNodeDisposition::Inline,
                            callback,
                        )
                    })
                    .map(|block| LazyNode::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(LazyNode::new_no_op())
    }

    /// Records a new inline lazy node with the given `name` and `callback`.
    pub fn record_lazy_values<'b, F>(&self, name: impl Into<StringReference<'b>>, callback: F)
    where
        F: Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send + 'static,
    {
        let property = self.create_lazy_values(name, callback);
        self.record(property);
    }

    /// Creates a lazy node from the given VMO.
    #[must_use]
    pub fn create_lazy_child_from_vmo<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        vmo: Arc<zx::Vmo>,
    ) -> LazyNode {
        self.create_lazy_child(name, move || {
            let vmo_clone = vmo.clone();
            async move { Ok(Inspector::no_op_from_vmo(vmo_clone)) }.boxed()
        })
    }

    /// Records a lazy node from the given VMO.
    pub fn record_lazy_child_from_vmo<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        vmo: Arc<zx::Vmo>,
    ) {
        self.record_lazy_child(name, move || {
            let vmo_clone = vmo.clone();
            async move { Ok(Inspector::no_op_from_vmo(vmo_clone)) }.boxed()
        });
    }

    /// Add a string property to this node.
    #[must_use]
    pub fn create_string<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        value: impl AsRef<str>,
    ) -> StringProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_property(
                            name,
                            value.as_ref().as_bytes(),
                            PropertyFormat::String,
                            inner_ref.block_index,
                        )
                    })
                    .map(|block| StringProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(StringProperty::new_no_op())
    }

    /// Creates and saves a string property for the lifetime of the node.
    pub fn record_string<'b>(&self, name: impl Into<StringReference<'b>>, value: impl AsRef<str>) {
        let property = self.create_string(name, value);
        self.record(property);
    }

    /// Add a byte vector property to this node.
    #[must_use]
    pub fn create_bytes<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        value: impl AsRef<[u8]>,
    ) -> BytesProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| {
                        state.create_property(
                            name,
                            value.as_ref(),
                            PropertyFormat::Bytes,
                            inner_ref.block_index,
                        )
                    })
                    .map(|block| BytesProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(BytesProperty::new_no_op())
    }

    /// Creates and saves a bytes property for the lifetime of the node.
    pub fn record_bytes<'b>(&self, name: impl Into<StringReference<'b>>, value: impl AsRef<[u8]>) {
        let property = self.create_bytes(name, value);
        self.record(property);
    }

    /// Add a bool property to this node.
    #[must_use]
    pub fn create_bool<'b>(
        &self,
        name: impl Into<StringReference<'b>>,
        value: bool,
    ) -> BoolProperty {
        self.inner
            .inner_ref()
            .and_then(|inner_ref| {
                inner_ref
                    .state
                    .try_lock()
                    .and_then(|mut state| state.create_bool(name, value, inner_ref.block_index))
                    .map(|block| BoolProperty::new(inner_ref.state.clone(), block.index()))
                    .ok()
            })
            .unwrap_or(BoolProperty::new_no_op())
    }

    /// Creates and saves a bool property for the lifetime of the node.
    pub fn record_bool<'b>(&self, name: impl Into<StringReference<'b>>, value: bool) {
        let property = self.create_bool(name, value);
        self.record(property);
    }

    /// Returns the [`Block`][Block] associated with this value.
    #[cfg(test)]
    pub(crate) fn get_block(&self) -> Option<Block<Arc<Mapping>>> {
        self.inner.inner_ref().and_then(|inner_ref| {
            inner_ref
                .state
                .try_lock()
                .and_then(|state| state.heap().get_block(inner_ref.block_index))
                .ok()
        })
    }

    /// Creates a new root node.
    pub(crate) fn new_root(state: State) -> Node {
        Node::new(state, 0)
    }

    /// Returns the inner state where operations in this node write.
    pub(crate) fn state(&self) -> Option<State> {
        self.inner.inner_ref().map(|inner_ref| inner_ref.state.clone())
    }
}

#[derive(Default, Debug)]
pub(crate) struct InnerNodeType;

impl InnerType for InnerNodeType {
    // Each node has a list of recorded values.
    type Data = ValueList;

    fn free(state: &State, block_index: u32) -> Result<(), Error> {
        if block_index == 0 {
            return Ok(());
        }
        let mut state_lock = state.try_lock()?;
        state_lock.free_value(block_index).map_err(|err| Error::free("node", block_index, err))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        reader,
        writer::{testing_utils::get_state, Error, NumericProperty},
    };
    use diagnostics_hierarchy::{assert_data_tree, DiagnosticsHierarchy};
    use fuchsia_zircon::{AsHandleRef, Peered};
    use inspect_format::BlockType;
    use std::convert::TryFrom;

    #[test]
    fn node() {
        // Create and use a default value.
        let default = Node::default();
        default.record_int("a", 0);

        let state = get_state(4096);
        let root = Node::new_root(state);
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        assert_eq!(node_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_block.child_count().unwrap(), 0);
        {
            let child = node.create_child("child");
            let child_block = child.get_block().unwrap();
            assert_eq!(child_block.block_type(), BlockType::NodeValue);
            assert_eq!(child_block.child_count().unwrap(), 0);
            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn node_no_op_clone_weak() {
        let default = Node::default();
        assert!(!default.is_valid());
        let weak = default.clone_weak();
        assert!(!weak.is_valid());
        let _ = weak.create_child("child");
        std::mem::drop(default);
        let _ = weak.create_uint("age", 1337);
        assert!(!weak.is_valid());
    }

    #[test]
    fn node_clone_weak() {
        let state = get_state(4096);
        let root = Node::new_root(state);
        let node = root.create_child("node");
        let node_weak = node.clone_weak();
        let node_weak_2 = node_weak.clone_weak(); // Weak from another weak

        let node_block = node.get_block().unwrap();
        assert_eq!(node_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_block.child_count().unwrap(), 0);
        let node_weak_block = node.get_block().unwrap();
        assert_eq!(node_weak_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_weak_block.child_count().unwrap(), 0);
        let node_weak_2_block = node.get_block().unwrap();
        assert_eq!(node_weak_2_block.block_type(), BlockType::NodeValue);
        assert_eq!(node_weak_2_block.child_count().unwrap(), 0);

        let child_from_strong = node.create_child("child");
        let child = node_weak.create_child("child_1");
        let child_2 = node_weak_2.create_child("child_2");
        std::mem::drop(node_weak_2);
        assert_eq!(node_weak_block.child_count().unwrap(), 3);
        std::mem::drop(child_from_strong);
        assert_eq!(node_weak_block.child_count().unwrap(), 2);
        std::mem::drop(child);
        assert_eq!(node_weak_block.child_count().unwrap(), 1);
        assert!(node_weak.is_valid());
        assert!(child_2.is_valid());
        std::mem::drop(node);
        assert!(!node_weak.is_valid());
        let _ = node_weak.create_child("orphan");
        let _ = child_2.create_child("orphan");
    }

    #[test]
    fn dummy_partialeq() {
        let inspector = Inspector::new();
        let root = inspector.root();

        // Types should all be equal to another type. This is to enable clients
        // with inspect types in their structs be able to derive PartialEq and
        // Eq smoothly.
        assert_eq!(root, &root.create_child("child1"));
        assert_eq!(root.create_int("property1", 1), root.create_int("property2", 2));
        assert_eq!(root.create_double("property1", 1.0), root.create_double("property2", 2.0));
        assert_eq!(root.create_uint("property1", 1), root.create_uint("property2", 2));
        assert_eq!(
            root.create_string("property1", "value1"),
            root.create_string("property2", "value2")
        );
        assert_eq!(
            root.create_bytes("property1", b"value1"),
            root.create_bytes("property2", b"value2")
        );
    }

    #[test]
    fn inspector_lazy_from_vmo() {
        let inspector = Inspector::new();
        inspector.root().record_uint("test", 3);

        let embedded_inspector = Inspector::new();
        embedded_inspector.root().record_uint("test2", 4);
        let vmo = embedded_inspector.duplicate_vmo().unwrap();

        inspector.root().record_lazy_child_from_vmo("lazy", Arc::new(vmo));
        assert_data_tree!(inspector, root: {
            test: 3u64,
            lazy: {
                test2: 4u64,
            }
        });
    }

    #[test]
    fn record() {
        let inspector = Inspector::new();
        let property = inspector.root().create_uint("a", 1);
        inspector.root().record_uint("b", 2);
        {
            let child = inspector.root().create_child("child");
            child.record(property);
            child.record_double("c", 3.14);
            assert_data_tree!(inspector, root: {
                a: 1u64,
                b: 2u64,
                child: {
                    c: 3.14,
                }
            });
        }
        // `child` went out of scope, meaning it was deleted.
        // Property `a` should be gone as well, given that it was being tracked by `child`.
        assert_data_tree!(inspector, root: {
            b: 2u64,
        });
    }

    #[test]
    fn record_child() {
        let inspector = Inspector::new();
        inspector.root().record_child("test", |node| {
            node.record_int("a", 1);
        });
        assert_data_tree!(inspector, root: {
            test: {
                a: 1i64,
            }
        })
    }

    #[test]
    fn record_weak() {
        let inspector = Inspector::new();
        let main = inspector.root().create_child("main");
        let main_weak = main.clone_weak();
        let property = main_weak.create_uint("a", 1);

        // Ensure either the weak or strong reference can be used for recording
        main_weak.record_uint("b", 2);
        main.record_uint("c", 3);
        {
            let child = main_weak.create_child("child");
            child.record(property);
            child.record_double("c", 3.14);
            assert_data_tree!(inspector, root: { main: {
                a: 1u64,
                b: 2u64,
                c: 3u64,
                child: {
                    c: 3.14,
                }
            }});
        }
        // `child` went out of scope, meaning it was deleted.
        // Property `a` should be gone as well, given that it was being tracked by `child`.
        assert_data_tree!(inspector, root: { main: {
            b: 2u64,
            c: 3u64
        }});
        std::mem::drop(main);
        // Recording after dropping a strong reference is a no-op
        main_weak.record_double("d", 1.0);
        // Verify that dropping a strong reference cleans up the state
        assert_data_tree!(inspector, root: { });
    }

    #[fuchsia::test]
    async fn atomic_update_reader() {
        let inspector = Inspector::new();

        // Spawn a read thread that holds a duplicate handle to the VMO that will be written.
        let vmo = inspector.duplicate_vmo().expect("duplicate vmo handle");
        let (p1, p2) = zx::EventPair::create().unwrap();

        macro_rules! notify_and_wait_reader {
            () => {
                p1.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).unwrap();
                p1.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE).unwrap();
                p1.signal_handle(zx::Signals::USER_0, zx::Signals::NONE).unwrap();
            };
        }

        macro_rules! wait_and_notify_writer {
            ($code:block) => {
              p2.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE).unwrap();
              p2.signal_handle(zx::Signals::USER_0, zx::Signals::NONE).unwrap();
              $code
              p2.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).unwrap();
            }
        }

        let thread = std::thread::spawn(move || {
            // Before running the atomic update.
            wait_and_notify_writer! {{
                let hierarchy: DiagnosticsHierarchy<String> =
                    reader::PartialNodeHierarchy::try_from(&vmo).unwrap().into();
                assert_eq!(hierarchy, DiagnosticsHierarchy::new_root());
            }};
            // After: create_child("child"): Assert that the VMO is in use (locked) and we can't
            // read.
            wait_and_notify_writer! {{
                assert!(reader::PartialNodeHierarchy::try_from(&vmo).is_err());
            }};
            // After: record_int("a"): Assert that the VMO is in use (locked) and we can't
            // read.
            wait_and_notify_writer! {{
                assert!(reader::PartialNodeHierarchy::try_from(&vmo).is_err());
            }};
            // After: record_int("b"): Assert that the VMO is in use (locked) and we can't
            // read.
            wait_and_notify_writer! {{
                assert!(reader::PartialNodeHierarchy::try_from(&vmo).is_err());
            }};
            // After atomic update
            wait_and_notify_writer! {{
                let hierarchy: DiagnosticsHierarchy<String> =
                    reader::PartialNodeHierarchy::try_from(&vmo).unwrap().into();
                assert_data_tree!(hierarchy, root: {
                   value: 2i64,
                   child: {
                       a: 1i64,
                       b: 2i64,
                   }
                });
            }};
        });

        // Perform the atomic update
        let mut child = Node::default();
        notify_and_wait_reader!();
        let int_val = inspector.root().create_int("value", 1);
        inspector
            .root()
            .atomic_update(|node| {
                // Intentionally make this slow to assert an atomic update in the reader.
                child = node.create_child("child");
                notify_and_wait_reader!();
                child.record_int("a", 1);
                notify_and_wait_reader!();
                child.record_int("b", 2);
                notify_and_wait_reader!();
                int_val.add(1);
                Ok::<(), Error>(())
            })
            .expect("successful atomic update");
        notify_and_wait_reader!();

        // Wait for the reader thread to successfully finish.
        let _ = thread.join();

        // Ensure that the variable that we mutated internally can be used.
        child.record_int("c", 3);
        assert_data_tree!(inspector, root: {
            value: 2i64,
            child: {
                a: 1i64,
                b: 2i64,
                c: 3i64,
            }
        });
    }
}
