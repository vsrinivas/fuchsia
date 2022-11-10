// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::storage::BlobfsInstance,
    fidl_fuchsia_io as fio,
    fuchsia_runtime::{take_startup_handle, HandleType},
    std::ops::Drop,
    tracing::info,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
        pseudo_directory, remote::remote_dir,
    },
};

pub struct FSHost {
    blobfs: BlobfsInstance,
}

impl FSHost {
    pub async fn new(fvm_path: &str, blobfs_mountpoint: &str) -> Self {
        info!(path = %fvm_path, "Starting blobfs from FVM image");
        let mut blobfs = BlobfsInstance::new_from_resource(fvm_path).await;
        info!(mountpoint = %blobfs_mountpoint, "Mounting blobfs");
        blobfs.mount(blobfs_mountpoint).await;

        Self { blobfs }
    }

    pub async fn serve(&self) {
        info!("Preparing services via VFS");

        let out_dir = pseudo_directory! {
            "blob" => remote_dir(self.blobfs.open_root_dir()),
        };
        let scope = ExecutionScope::new();
        out_dir.open(
            scope.clone(),
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::RIGHT_EXECUTABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            take_startup_handle(HandleType::DirectoryRequest.into()).unwrap().into(),
        );

        info!("Serving services via VFS");

        let () = scope.wait().await;
    }
}

impl Drop for FSHost {
    fn drop(&mut self) {
        self.blobfs.unmount();
    }
}
