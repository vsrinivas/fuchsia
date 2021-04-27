// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::core::{Core, ObjectRef, OnAdded, Property};

#[derive(Debug, Default)]
pub struct StateMachineComponent {
    name: Property<String>,
}

impl ObjectRef<'_, StateMachineComponent> {
    pub fn name(&self) -> String {
        self.name.get()
    }

    pub fn set_name(&self, name: String) {
        self.name.set(name);
    }
}

impl Core for StateMachineComponent {
    properties![(138, name, set_name)];
}

impl OnAdded for ObjectRef<'_, StateMachineComponent> {
    on_added!();
}
