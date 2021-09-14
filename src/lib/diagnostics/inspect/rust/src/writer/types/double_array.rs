// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::writer::{
    ArrayProperty, Inner, InnerValueType, InspectType, InspectTypeInternal, State,
};
use tracing::error;

#[cfg(test)]
use {inspect_format::Block, mapped_vmo::Mapping, std::sync::Arc};

/// Inspect double array data type.
///
/// NOTE: do not rely on PartialEq implementation for true comparison.
/// Instead leverage the reader.
///
/// NOTE: Operations on a Default value are no-ops.
#[derive(Debug, PartialEq, Eq, Default)]
pub struct DoubleArrayProperty {
    pub(crate) inner: Inner<InnerValueType>,
}

impl InspectType for DoubleArrayProperty {}

impl InspectTypeInternal for DoubleArrayProperty {
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

impl ArrayProperty for DoubleArrayProperty {
    type Type = f64;

    fn set(&self, index: usize, value: f64) {
        if let Some(ref inner_ref) = self.inner.inner_ref() {
            inner_ref
                .state
                .try_lock()
                .and_then(|mut state| {
                    state.set_array_double_slot(inner_ref.block_index, index, value)
                })
                .unwrap_or_else(|err| {
                    error!(?err, "Failed to set property");
                });
        }
    }

    fn add(&self, index: usize, value: f64) {
        if let Some(ref inner_ref) = self.inner.inner_ref() {
            inner_ref
                .state
                .try_lock()
                .and_then(|mut state| {
                    state.add_array_double_slot(inner_ref.block_index, index, value)
                })
                .unwrap_or_else(|err| {
                    error!(?err, "Failed to add property");
                });
        }
    }

    fn subtract(&self, index: usize, value: f64) {
        if let Some(ref inner_ref) = self.inner.inner_ref() {
            inner_ref
                .state
                .try_lock()
                .and_then(|mut state| {
                    state.subtract_array_double_slot(inner_ref.block_index, index, value)
                })
                .unwrap_or_else(|err| {
                    error!(?err, "Failed to subtract property");
                });
        }
    }

    fn clear(&self) {
        if let Some(ref inner_ref) = self.inner.inner_ref() {
            inner_ref
                .state
                .try_lock()
                .and_then(|mut state| state.clear_array(inner_ref.block_index, 0))
                .unwrap_or_else(|e| {
                    error!("Failed to clear property. Error: {:?}", e);
                });
        }
    }
}

#[cfg(test)]
impl DoubleArrayProperty {
    /// Returns the [`Block`][Block] associated with this value.
    pub fn get_block(&self) -> Option<Block<Arc<Mapping>>> {
        self.inner.inner_ref().and_then(|inner_ref| {
            inner_ref
                .state
                .try_lock()
                .and_then(|state| state.heap().get_block(inner_ref.block_index))
                .ok()
        })
    }

    /// Returns the index of the value's block in the VMO.
    pub fn block_index(&self) -> u32 {
        self.inner.inner_ref().unwrap().block_index
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::writer::Inspector;

    #[test]
    fn test_double_array() {
        // Create and use a default value.
        let default = DoubleArrayProperty::default();
        default.add(1, 1.0);

        let inspector = Inspector::new();
        let root = inspector.root();
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let array = node.create_double_array("array_property", 5);
            let array_block = array.get_block().unwrap();

            array.set(0, 5.0);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 5.0);

            array.add(0, 5.3);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 10.3);

            array.subtract(0, 3.4);
            assert_eq!(array_block.array_get_double_slot(0).unwrap(), 6.9);

            array.set(1, 2.5);
            array.set(3, -3.1);

            for (i, value) in [6.9, 2.5, 0.0, -3.1, 0.0].iter().enumerate() {
                assert_eq!(array_block.array_get_double_slot(i).unwrap(), *value);
            }

            array.clear();
            for i in 0..5 {
                assert_eq!(0.0, array_block.array_get_double_slot(i).unwrap());
            }

            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }
}
