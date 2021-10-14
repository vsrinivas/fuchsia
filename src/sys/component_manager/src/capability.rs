// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{component::ComponentInstance, error::ModelError},
    ::routing::capability_source::CapabilitySourceInterface,
    async_trait::async_trait,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    std::path::PathBuf,
};

// TODO(https://fxbug.dev/71901): remove aliases once the routing lib has a stable API.
pub type CapabilitySource = CapabilitySourceInterface<ComponentInstance>;
pub type InternalCapability = ::routing::capability_source::InternalCapability;
pub type ComponentCapability = ::routing::capability_source::ComponentCapability;
pub type AggregateCapability = ::routing::capability_source::AggregateCapability;
pub type NamespaceCapabilities = ::routing::capability_source::NamespaceCapabilities;

/// Wrapper that might hold an `fasync::Task`. `#[must_use]` to make sure the client grabs the
/// task.
#[must_use]
pub struct OptionalTask {
    task: Option<fasync::Task<()>>,
}

impl OptionalTask {
    pub fn take(self) -> Option<fasync::Task<()>> {
        self.task
    }
}

impl From<Option<fasync::Task<()>>> for OptionalTask {
    fn from(task: Option<fasync::Task<()>>) -> Self {
        Self { task }
    }
}

impl From<fasync::Task<()>> for OptionalTask {
    fn from(task: fasync::Task<()>) -> Self {
        Some(task).into()
    }
}

/// The server-side of a capability implements this trait.
/// Multiple `CapabilityProvider` objects can compose with one another for a single
/// capability request. For example, a `CapabitilityProvider` can be interposed
/// between the primary `CapabilityProvider and the client for the purpose of
/// logging and testing. A `CapabilityProvider` is typically provided by a
/// corresponding `Hook` in response to the `CapabilityRouted` event.
/// A capability provider is used exactly once as a result of exactly one route.
#[async_trait]
pub trait CapabilityProvider: Send + Sync {
    /// Binds a server end of a zx::Channel to the provided capability.  If the capability is a
    /// directory, then `flags`, `open_mode` and `relative_path` will be propagated along to open
    /// the appropriate directory.
    ///
    /// May return a `fuchsia_async::Task` to serve this capability. The caller should ensure that
    /// it stays live for an appropriate scope associated with the capability.
    async fn open(
        self: Box<Self>,
        flags: u32,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<OptionalTask, ModelError>;
}
