// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{model::Model, resolve_component::ResolveComponent},
    moniker::AbsoluteMoniker,
    std::sync::Weak,
};

pub struct ResolveComponentFactory {
    model: Weak<Model>,
}

impl ResolveComponentFactory {
    pub fn new(model: Weak<Model>) -> Self {
        Self { model }
    }

    pub fn create(&self, root: &AbsoluteMoniker) -> ResolveComponent {
        ResolveComponent::new(self.model.clone(), root.clone())
    }
}
