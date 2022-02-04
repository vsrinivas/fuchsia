// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{ComponentInstance, StartReason},
        error::ModelError,
        model::Model,
    },
    async_trait::async_trait,
    moniker::AbsoluteMoniker,
    std::sync::{Arc, Weak},
};

/// A trait to enable support for different `start()` implementations. This is used,
/// for example, for testing code that depends on `start()`, but no other `Model`
/// functionality.
#[async_trait]
pub trait Starter: Send + Sync {
    async fn start_instance<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
        reason: &'a StartReason,
    ) -> Result<Arc<ComponentInstance>, ModelError>;
}

#[async_trait]
impl Starter for Arc<Model> {
    /// Starts the component instance with the specified moniker. This has the following effects:
    /// - Binds to the parent instance.
    /// - Starts the component instance, if it is not already running and not shut down.
    /// - Starts any descendant component instances that need to be eagerly started.
    // TODO: This function starts the parent component, but doesn't track the bindings anywhere.
    // This means that when the child stops and the parent has no other reason to run, we won't
    // stop the parent. To solve this, we need to track the bindings.
    async fn start_instance<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
        reason: &'a StartReason,
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        start_moniker(self, abs_moniker, reason).await
    }
}

#[async_trait]
impl Starter for Weak<Model> {
    async fn start_instance<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
        reason: &'a StartReason,
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        if let Some(model) = self.upgrade() {
            model.start_instance(abs_moniker, reason).await
        } else {
            Err(ModelError::ModelNotAvailable)
        }
    }
}

/// Starts the component instance in the given component if it's not already running.
/// Returns the component that was bound to.
pub async fn start_moniker<'a>(
    model: &'a Arc<Model>,
    abs_moniker: &'a AbsoluteMoniker,
    reason: &StartReason,
) -> Result<Arc<ComponentInstance>, ModelError> {
    let component = model.look_up(abs_moniker).await?;
    component.start(reason).await?;
    Ok(component)
}
