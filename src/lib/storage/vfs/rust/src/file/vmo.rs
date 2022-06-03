// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! File nodes backed by a VMO.  These are useful for cases when individual read/write operation
//! actions need to be visible across all the connections to the same file.

pub mod asynchronous;

/// A read-only file interface that accepts a zx::Vmo upon construction can be found below.  All
/// other support is currently via an asynchronous interface.
pub use asynchronous::{
    read_only, read_only_const, read_only_static, read_write,
    simple_init_vmo_resizable_with_capacity, simple_init_vmo_with_capacity,
};

use crate::{
    common::send_on_open_with_error,
    directory::entry::EntryInfo,
    execution_scope::ExecutionScope,
    file::{
        vmo::{
            asynchronous::VmoFileState,
            connection::{io1::VmoFileConnection, AsyncInitVmo, VmoFileInterface},
        },
        DirectoryEntry,
    },
    path::Path,
};

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    futures::lock::{Mutex, MutexLockFuture},
    std::sync::Arc,
};

pub(self) mod connection;

/// A read-only file backed by a VMO.
pub struct ReadOnlyVmoFile {
    state: Mutex<VmoFileState>,
}

impl ReadOnlyVmoFile {
    /// Returns a new read-only file backed by the provided VMO.
    pub fn new(vmo: zx::Vmo, size: u64) -> Self {
        Self {
            state: Mutex::new(VmoFileState::Initialized {
                vmo,
                size,
                vmo_size: size,
                capacity: size,
                connection_count: 0,
            }),
        }
    }
}

impl VmoFileInterface for ReadOnlyVmoFile {
    fn init_vmo(self: Arc<Self>) -> AsyncInitVmo {
        unreachable!();
    }

    fn state(&self) -> MutexLockFuture<VmoFileState> {
        self.state.lock()
    }

    fn is_readable(&self) -> bool {
        true
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

impl DirectoryEntry for ReadOnlyVmoFile {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        _mode: u32,
        path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        if !path.is_empty() {
            send_on_open_with_error(flags, server_end, zx::Status::NOT_DIR);
            return;
        }

        VmoFileConnection::create_connection(scope.clone(), self, flags, server_end);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::ReadOnlyVmoFile,
        crate::{assert_close, assert_read, file::test_utils::run_server_client},
        fidl_fuchsia_io as fio, fuchsia_zircon as zx,
        std::sync::Arc,
    };

    #[test]
    fn read_only_vmo_file() {
        let vmo = zx::Vmo::create(1024).expect("create failed");
        let data = b"Read only str";
        vmo.write(data, 0).expect("write failed");
        run_server_client(
            fio::OpenFlags::RIGHT_READABLE,
            Arc::new(ReadOnlyVmoFile::new(vmo, data.len() as u64)),
            |proxy| async move {
                assert_read!(proxy, "Read only str");
                assert_close!(proxy);
            },
        );
    }
}
