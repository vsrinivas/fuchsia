// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey},
        component::ComponentInstance,
        error::ModelError,
    },
    async_trait::async_trait,
    std::sync::Arc,
};

/// Stops a component instance.
pub struct StopAction {}

impl StopAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for StopAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        component.stop_instance(false).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Stop
    }
}
