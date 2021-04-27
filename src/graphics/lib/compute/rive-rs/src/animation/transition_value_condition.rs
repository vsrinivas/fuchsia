// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::TransitionCondition,
    core::{Core, ObjectRef, OnAdded, Property},
};

#[derive(Debug, Default)]
pub struct TransitionValueCondition {
    transition_condition: TransitionCondition,
    op_value: Property<u64>,
}

impl ObjectRef<'_, TransitionValueCondition> {
    pub fn op_value(&self) -> u64 {
        self.op_value.get()
    }

    pub fn set_op_value(&self, op_value: u64) {
        self.op_value.set(op_value);
    }
}

impl Core for TransitionValueCondition {
    parent_types![(transition_condition, TransitionCondition)];

    properties![(156, op_value, set_op_value), transition_condition];
}

impl OnAdded for ObjectRef<'_, TransitionValueCondition> {
    on_added!(TransitionCondition);
}
