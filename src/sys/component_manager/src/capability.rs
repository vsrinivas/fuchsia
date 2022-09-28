// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{component::ComponentInstance, error::ModelError},
    ::routing::capability_source::CapabilitySourceInterface,
    async_trait::async_trait,
    cm_task_scope::TaskScope,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    std::path::PathBuf,
};

pub type CapabilitySource = CapabilitySourceInterface<ComponentInstance>;
/// The server-side of a capability implements this trait.
/// Multiple `CapabilityProvider` objects can compose with one another for a single
/// capability request. For example, a `CapabilityProvider` can be interposed
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
        task_scope: TaskScope,
        flags: fio::OpenFlags,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError>;
}

/// The only flags that are accepted by `CapabilityProvider` implementations.
pub const PERMITTED_FLAGS: fio::OpenFlags = fio::OpenFlags::empty()
    .union(fio::OpenFlags::RIGHT_READABLE)
    .union(fio::OpenFlags::RIGHT_WRITABLE)
    .union(fio::OpenFlags::POSIX_WRITABLE);
