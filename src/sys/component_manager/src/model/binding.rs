// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{BindReason, ComponentInstance},
        error::ModelError,
        model::Model,
    },
    async_trait::async_trait,
    moniker::PartialAbsoluteMoniker,
    std::sync::{Arc, Weak},
};

/// A trait to enable support for different `bind()` implementations. This is used,
/// for example, for testing code that depends on `bind()`, but no other `Model`
/// functionality.
#[async_trait]
pub trait Binder: Send + Sync {
    async fn bind<'a>(
        &'a self,
        abs_moniker: &'a PartialAbsoluteMoniker,
        reason: &'a BindReason,
    ) -> Result<Arc<ComponentInstance>, ModelError>;
}

#[async_trait]
impl Binder for Arc<Model> {
    /// Binds to the component instance with the specified moniker. This has the following effects:
    /// - Binds to the parent instance.
    /// - Starts the component instance, if it is not already running and not shut down.
    /// - Binds to any descendant component instances that need to be eagerly started.
    // TODO: This function starts the parent component, but doesn't track the bindings anywhere.
    // This means that when the child stops and the parent has no other reason to run, we won't
    // stop the parent. To solve this, we need to track the bindings.
    async fn bind<'a>(
        &'a self,
        abs_moniker: &'a PartialAbsoluteMoniker,
        reason: &'a BindReason,
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        bind_at_moniker(self, abs_moniker, reason).await
    }
}

#[async_trait]
impl Binder for Weak<Model> {
    async fn bind<'a>(
        &'a self,
        abs_moniker: &'a PartialAbsoluteMoniker,
        reason: &'a BindReason,
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        if let Some(model) = self.upgrade() {
            model.bind(abs_moniker, reason).await
        } else {
            Err(ModelError::ModelNotAvailable)
        }
    }
}

/// Binds to the component instance in the given component, starting it if it's not already running.
/// Returns the component that was bound to.
pub async fn bind_at_moniker<'a>(
    model: &'a Arc<Model>,
    abs_moniker: &'a PartialAbsoluteMoniker,
    reason: &BindReason,
) -> Result<Arc<ComponentInstance>, ModelError> {
    let component = model.look_up(abs_moniker).await?;
    component.bind(reason).await?;
    Ok(component)
}
