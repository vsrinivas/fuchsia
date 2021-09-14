// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::writer::{ArrayProperty, HistogramProperty, IntArrayProperty, Node, StringReference};
use diagnostics_hierarchy::{ArrayFormat, ExponentialHistogramParams};
use inspect_format::constants;
use tracing::error;

#[cfg(test)]
use {inspect_format::Block, mapped_vmo::Mapping, std::sync::Arc};

#[derive(Debug)]
/// An exponential histogram property for int values.
pub struct IntExponentialHistogramProperty {
    array: IntArrayProperty,
    floor: i64,
    initial_step: i64,
    step_multiplier: i64,
    slots: usize,
}

impl IntExponentialHistogramProperty {
    pub(crate) fn new<'b>(
        name: impl Into<StringReference<'b>>,
        params: ExponentialHistogramParams<i64>,
        parent: &Node,
    ) -> Self {
        let slots = params.buckets + constants::EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS;
        let array =
            parent.create_int_array_internal(name, slots, ArrayFormat::ExponentialHistogram);
        array.set(0, params.floor);
        array.set(1, params.initial_step);
        array.set(2, params.step_multiplier);
        Self {
            floor: params.floor,
            initial_step: params.initial_step,
            step_multiplier: params.step_multiplier,
            slots,
            array,
        }
    }

    fn get_index(&self, value: i64) -> usize {
        let mut current_floor = self.floor;
        let mut offset = self.initial_step;
        // Start in the underflow index.
        let mut index = constants::EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS - 2;
        while value >= current_floor && index < self.slots - 1 {
            current_floor = self.floor + offset;
            offset *= self.step_multiplier;
            index += 1;
        }
        index as usize
    }

    #[cfg(test)]
    fn get_block(&self) -> Option<Block<Arc<Mapping>>> {
        self.array.get_block()
    }
}

impl HistogramProperty for IntExponentialHistogramProperty {
    type Type = i64;

    fn insert(&self, value: i64) {
        self.insert_multiple(value, 1);
    }

    fn insert_multiple(&self, value: i64, count: usize) {
        self.array.add(self.get_index(value), count as i64);
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
                        constants::EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS - 2,
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
    use crate::{writer::Inspector, ExponentialHistogramParams};

    #[test]
    fn test_int_exp_histogram() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let node = root.create_child("node");
        let node_block = node.get_block().unwrap();
        {
            let int_histogram = node.create_int_exponential_histogram(
                "int-histogram",
                ExponentialHistogramParams {
                    floor: 1,
                    initial_step: 1,
                    step_multiplier: 2,
                    buckets: 4,
                },
            );
            int_histogram.insert_multiple(-1, 2); // underflow
            int_histogram.insert(8);
            int_histogram.insert(500); // overflow
            let block = int_histogram.get_block().unwrap();
            for (i, value) in [1, 1, 2, 2, 0, 0, 0, 1, 1].iter().enumerate() {
                assert_eq!(block.array_get_int_slot(i).unwrap(), *value);
            }

            assert_eq!(node_block.child_count().unwrap(), 1);
        }
        assert_eq!(node_block.child_count().unwrap(), 0);
    }

    #[test]
    fn exp_histogram_insert() {
        let inspector = Inspector::new();
        let root = inspector.root();
        let hist = root.create_int_exponential_histogram(
            "test",
            ExponentialHistogramParams {
                floor: 0,
                initial_step: 2,
                step_multiplier: 4,
                buckets: 4,
            },
        );
        for i in -200..200 {
            hist.insert(i);
        }
        let block = hist.get_block().unwrap();
        assert_eq!(block.array_get_int_slot(0).unwrap(), 0);
        assert_eq!(block.array_get_int_slot(1).unwrap(), 2);
        assert_eq!(block.array_get_int_slot(2).unwrap(), 4);

        // Buckets
        let i = 3;
        assert_eq!(block.array_get_int_slot(i).unwrap(), 200);
        assert_eq!(block.array_get_int_slot(i + 1).unwrap(), 2);
        assert_eq!(block.array_get_int_slot(i + 2).unwrap(), 6);
        assert_eq!(block.array_get_int_slot(i + 3).unwrap(), 24);
        assert_eq!(block.array_get_int_slot(i + 4).unwrap(), 96);
        assert_eq!(block.array_get_int_slot(i + 5).unwrap(), 72);
    }
}
