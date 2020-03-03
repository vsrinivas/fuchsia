// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::file::vmo::asynchronous::{AsyncFileState, ConsumeVmoResult, InitVmoResult};

use {
    fuchsia_zircon::Vmo,
    futures::{future::BoxFuture, lock::MutexLockFuture},
    std::sync::Arc,
};

pub mod io1;

/// Result of the [`FileConnectionApi::init_vmo()`] call.
///
/// At the moment `init_vmo` will always return a future.  It will be executed using
/// `ExecutionScope` for the first attached connection.
pub type AsyncInitVmo = BoxFuture<'static, InitVmoResult>;

/// Result of the [`FileConnectionApi::consume_vmo()`] call.
pub enum AsyncConsumeVmo {
    Immediate(ConsumeVmoResult),
    Future(BoxFuture<'static, ConsumeVmoResult>),
}

/// Interface that a connection uses to interact with a VMO file.  It erases the parametricity
/// over the specific handler types.
pub(in crate::file::vmo) trait FileConnectionApi: Send + Sync {
    fn init_vmo(self: Arc<Self>) -> AsyncInitVmo;

    fn consume_vmo(self: Arc<Self>, vmo: Vmo) -> AsyncConsumeVmo;

    fn state(&self) -> MutexLockFuture<AsyncFileState>;
}
