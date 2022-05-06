// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use futures::lock::Mutex;
use std::sync::Arc;

/// Closure definition for an action that can be triggered by ActionFuse.
pub type TriggeredAction = Box<dyn FnOnce() + Send + Sync + 'static>;
/// The reference-counted handle to an ActionFuse. When all references go out of
/// scope, the action will be triggered (if not defused).
pub type ActionFuseHandle = Arc<Mutex<ActionFuse>>;

/// ActionFuseBuilder allows creation of ActionFuses. Note that all parameters
/// are completely optional to the builder. A fuse with no action or chained
/// fuse is valid.
pub(crate) struct ActionFuseBuilder {
    actions: Vec<TriggeredAction>,
    chained_fuses: Vec<ActionFuseHandle>,
}

impl ActionFuseBuilder {
    pub(crate) fn new() -> Self {
        ActionFuseBuilder { actions: vec![], chained_fuses: vec![] }
    }

    /// Chains supplied fuse to the built fuse. Multiple fuses can be added
    /// and all will be triggered upon this fuse being dropped.
    #[cfg(test)]
    pub(crate) fn chain_fuse(mut self, fuse: ActionFuseHandle) -> Self {
        self.chained_fuses.push(fuse);
        self
    }

    /// Shortcut for chaining multiple fuses at once.
    pub(crate) fn chain_fuses(mut self, fuses: Vec<ActionFuseHandle>) -> Self {
        for fuse in fuses {
            self.chained_fuses.push(fuse);
        }
        self
    }

    /// Adds an action to be executed once dropped.
    pub(crate) fn add_action(mut self, action: TriggeredAction) -> Self {
        self.actions.push(action);
        self
    }

    /// Generates fuse based on parameters.
    pub(crate) fn build(self) -> ActionFuseHandle {
        ActionFuse::create(self.actions, self.chained_fuses)
    }
}

/// ActionFuse is a wrapper around a triggered action (a closure with no
/// arguments and no return value). This action is invoked once the fuse goes
/// out of scope (via the Drop trait). An ActionFuse can be defused, preventing
/// the action from automatically invoking when going out of scope.
pub struct ActionFuse {
    /// An optional action that will be invoked when the ActionFuse goes out of
    /// scope.
    actions: Vec<TriggeredAction>,

    /// A list of optional fuses that follows this fuse. It will be defused in
    /// tandem and dropped at the same time.
    chained_fuses: Vec<ActionFuseHandle>,
}

impl ActionFuse {
    /// Returns an ActionFuse reference with the given TriggerAction.
    pub(super) fn create(
        actions: Vec<TriggeredAction>,
        chained_fuses: Vec<ActionFuseHandle>,
    ) -> ActionFuseHandle {
        Arc::new(Mutex::new(ActionFuse { actions, chained_fuses }))
    }

    /// Suppresses the action from automatically executing.
    pub(crate) fn defuse(handle: ActionFuseHandle) {
        fasync::Task::spawn(async move {
            let mut fuse = handle.lock().await;
            fuse.actions.clear();
            for chained_fuse in &fuse.chained_fuses {
                ActionFuse::defuse(chained_fuse.clone());
            }
        })
        .detach();
    }
}

impl Drop for ActionFuse {
    fn drop(&mut self) {
        while let Some(action) = self.actions.pop() {
            (action)();
        }
    }
}
