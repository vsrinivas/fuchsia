// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{pkgfs::PkgfsInstance, storage::BlobfsInstance},
    fidl_fuchsia_io::{OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_async::futures::StreamExt,
    fuchsia_component::server::ServiceFs,
    fuchsia_merkle::MerkleTree,
    fuchsia_syslog::fx_log_info,
    io_util::directory::open_in_namespace,
    std::{fs::File, ops::Drop},
};

pub struct FSHost {
    blobfs: BlobfsInstance,
    pkgfs: Option<PkgfsInstance>,
}

impl FSHost {
    pub async fn new(fvm_path: &str, blobfs_mountpoint: &str, system_image_path: &str) -> Self {
        fx_log_info!("Starting blobfs from FVM image at {}", fvm_path);
        let mut blobfs = BlobfsInstance::new_from_resource(fvm_path).await;
        fx_log_info!("Mounting blobfs at {}", blobfs_mountpoint);
        blobfs.mount(blobfs_mountpoint);
        let blobfs_dir = open_in_namespace(
            blobfs_mountpoint,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_RIGHT_EXECUTABLE,
        )
        .unwrap();
        let mut system_image_file = File::open(system_image_path).unwrap();
        let system_image_merkle = MerkleTree::from_reader(&mut system_image_file).unwrap().root();

        fx_log_info!("Starting pkgfs with system image merkle {}", system_image_merkle.to_string());
        let pkgfs = PkgfsInstance::new(blobfs_dir, system_image_merkle);

        Self { blobfs, pkgfs: Some(pkgfs) }
    }

    pub async fn serve(&self) {
        fx_log_info!("Preparing ServiceFs");

        let mut fs = ServiceFs::new();
        fs.add_remote("blob", self.blobfs.open_root_dir());
        fs.add_remote("pkgfs", self.pkgfs.as_ref().unwrap().proxy());
        fs.take_and_serve_directory_handle().unwrap();

        fx_log_info!("Serving from ServiceFs");

        fs.collect::<()>().await;
    }
}

impl Drop for FSHost {
    fn drop(&mut self) {
        // Drop pkgfs before unmounting blobfs.
        self.pkgfs = None;
        self.blobfs.unmount();
    }
}
