// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::writer::{ArrayProperty, DoubleArrayProperty, HistogramProperty, Node, StringReference};
use diagnostics_hierarchy::{ArrayFormat, LinearHistogramParams};
use inspect_format::constants;
use tracing::error;

#[cfg(test)]
use {inspect_format::Block, mapped_vmo::Mapping, std::sync::Arc};

#[derive(Debug)]
/// A linear histogram property for double values.
pub struct DoubleLinearHistogramProperty {
    array: DoubleArrayProperty,
    floor: f64,
    slots: usize,
    step_size: f64,
}

impl DoubleLinearHistogramProperty {
    pub(crate) fn new<'b>(
        name: impl Into<StringReference<'b>>,
        params: LinearHistogramParams<f64>,
        parent: &Node,
    ) -> Self {
        let slots = params.buckets + constants::LINEAR_HISTOGRAM_EXTRA_SLOTS;
        let array = parent.create_double_array_internal(name, slots, ArrayFormat::LinearHistogram);
        array.set(0, params.floor);
        array.set(1, params.step_size);
        Self { floor: params.floor, step_size: params.step_size, slots, array }
    }

    fn get_index(&self, value: f64) -> usize {
        let mut current_floor = self.floor;
        // Start in the underflow index.
        let mut index = constants::LINEAR_HISTOGRAM_EXTRA_SLOTS - 2;
        while value >= current_floor && index < self.slots - 1 {
            current_floor += self.step_size;
            index += 1;
        }
        index as usize
    }

    #[cfg(test)]
    fn get_block(&self) -> Option<Block<Arc<Mapping>>> {
        self.array.get_block()
    }
}

impl HistogramProperty for DoubleLinearHistogramProperty {
    type Type = f64;

    fn insert(&self, value: f64) {
        self.insert_multiple(value, 1);
    }

    fn insert_multiple(&self, value: f64, count: usize) {
        self.array.add(self.get_index(value), count as f64);
    }

    fn clear(&self) {
        if let Some(ref inner_ref) = self.array.inner.inner_ref() {
            // Ensure we don't delete the array slots that contain histogram metadata.
            inner_ref
                .state
                .try_lock()
                .and_then(|mut state| {
                    // -2 = the overflow and underflow slots which still need to be cleared.
                    state.clear_array(
                        inner_ref.block_index,
                        constants::LINEAR_HISTOGRAM_EXTRA_SLOTS - 2,
                    )
                })
                .unwrap_or_else(|err| {
                    error!(?err, "Failed to clear property");
                });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{writer::Inspector, LinearHistogramParams};

    #[test]
    fn double_linear_histogram() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let double_histogram = node.create_double_linear_histogram(
                "double-histogram",
                LinearHistogramParams { floor: 10.0, step_size: 5.0, buckets: 5 },
            );
            double_histogram.insert_multiple(0.0, 2); // underflow
            double_histogram.insert(25.3);
            double_histogram.insert(500.0); // overflow
            let block = double_histogram.get_block().unwrap();
            for (i, value) in [10.0, 5.0, 2.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0].iter().enumerate() {
                assert_eq!(block.array_get_double_slot(i).unwrap(), *value);
            }

            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }
}
