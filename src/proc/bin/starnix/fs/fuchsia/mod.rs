// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased};

use crate::fs::FileHandle;

mod pipe;
mod remote;
mod syslog;

pub use pipe::*;
pub use remote::*;
pub use syslog::*;

/// Create a FileHandle from a zx::Handle.
#[allow(dead_code)] // Will be used by process initialization later.
pub fn create_file_from_handle(handle: zx::Handle) -> Result<FileHandle, zx::Status> {
    let info = handle.basic_info()?;
    match info.object_type {
        zx::ObjectType::SOCKET => FuchsiaPipe::from_socket(zx::Socket::from_handle(handle)),
        _ => Err(zx::Status::NOT_SUPPORTED),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_create_from_invalid_handle() {
        assert!(create_file_from_handle(zx::Handle::invalid()).is_err());
    }

    #[test]
    fn test_create_pipe_from_handle() {
        let (left_handle, right_handle) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        create_file_from_handle(left_handle.into_handle())
            .expect("failed to create left FileHandle");
        create_file_from_handle(right_handle.into_handle())
            .expect("failed to create right FileHandle");
    }
}
