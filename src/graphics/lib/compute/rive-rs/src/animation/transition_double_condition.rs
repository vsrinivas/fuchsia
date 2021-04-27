// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::TransitionValueCondition,
    core::{Core, ObjectRef, OnAdded, Property},
};

#[derive(Debug, Default)]
pub struct TransitionDoubleCondition {
    transition_double_condition: TransitionValueCondition,
    value: Property<f32>,
}

impl ObjectRef<'_, TransitionDoubleCondition> {
    pub fn value(&self) -> f32 {
        self.value.get()
    }

    pub fn set_value(&self, value: f32) {
        self.value.set(value);
    }
}

impl Core for TransitionDoubleCondition {
    parent_types![(transition_double_condition, TransitionValueCondition)];

    properties![(157, value, set_value), transition_double_condition];
}

impl OnAdded for ObjectRef<'_, TransitionDoubleCondition> {
    on_added!(TransitionValueCondition);
}
