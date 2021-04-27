// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::core::{Core, ObjectRef, OnAdded, Property};

#[derive(Debug, Default)]
pub struct TransitionCondition {
    input_id: Property<u64>,
}

impl ObjectRef<'_, TransitionCondition> {
    pub fn input_id(&self) -> u64 {
        self.input_id.get()
    }

    pub fn set_input_id(&self, input_id: u64) {
        self.input_id.set(input_id);
    }
}

impl Core for TransitionCondition {
    properties![(155, input_id, set_input_id)];
}

impl OnAdded for ObjectRef<'_, TransitionCondition> {
    on_added!();
}
