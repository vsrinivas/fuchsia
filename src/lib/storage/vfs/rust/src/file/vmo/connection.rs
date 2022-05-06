// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::file::vmo::asynchronous::{InitVmoResult, VmoFileState};

use {
    fidl_fuchsia_io as fio,
    futures::{future::BoxFuture, lock::MutexLockFuture},
    std::sync::Arc,
};

pub mod io1;

/// Result of the [`VmoFileInterface::init_vmo()`] call.
///
/// At the moment `init_vmo` will always return a future.  It will be executed using
/// `ExecutionScope` for the first attached connection.
pub type AsyncInitVmo = BoxFuture<'static, InitVmoResult>;

/// Interface that a connection uses to interact with a VMO file.  It erases the parametricity
/// over the specific handler types.
pub(in crate::file::vmo) trait VmoFileInterface: Send + Sync {
    fn init_vmo(self: Arc<Self>) -> AsyncInitVmo;

    fn state(&self) -> MutexLockFuture<VmoFileState>;

    fn is_readable(&self) -> bool {
        false
    }

    fn is_writable(&self) -> bool {
        false
    }

    fn is_executable(&self) -> bool {
        false
    }

    fn get_inode(&self) -> u64 {
        fio::INO_UNKNOWN
    }
}
