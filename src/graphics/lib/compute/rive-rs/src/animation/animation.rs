// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::LinearAnimation,
    core::{Core, CoreContext, ObjectRef, OnAdded, Property},
    status_code::StatusCode,
};

#[derive(Debug, Default)]
pub struct Animation {
    name: Property<String>,
}

impl ObjectRef<'_, Animation> {
    pub fn name(&self) -> String {
        self.name.get()
    }

    pub fn set_name(&self, name: String) {
        self.name.set(name);
    }
}

impl Core for Animation {
    properties![(55, name, set_name)];
}

impl OnAdded for ObjectRef<'_, Animation> {
    on_added!([import]);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        if let Some(linear_animation) = self.try_cast::<LinearAnimation>() {
            return linear_animation.on_added_dirty(context);
        }

        StatusCode::Ok
    }

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        if let Some(linear_animation) = self.try_cast::<LinearAnimation>() {
            return linear_animation.on_added_clean(context);
        }

        StatusCode::Ok
    }
}
