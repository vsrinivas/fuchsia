// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{lifecycle_controller::LifecycleController, model::Model},
    moniker::PartialAbsoluteMoniker,
    std::sync::Weak,
};

pub struct LifecycleControllerFactory {
    model: Weak<Model>,
}

impl LifecycleControllerFactory {
    pub fn new(model: Weak<Model>) -> Self {
        Self { model }
    }

    pub fn create(&self, root: &PartialAbsoluteMoniker) -> LifecycleController {
        LifecycleController::new(self.model.clone(), root.clone())
    }
}
