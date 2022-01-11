// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::{create_proxy, Proxy},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, CLONE_FLAG_SAME_RIGHTS},
    fuchsia_merkle::Hash,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_syslog::fx_log_info,
    scoped_task::{job_default, spawn_etc, Scoped},
    std::ffi::{CStr, CString},
};

const PKGSVR_PATH: &str = "/pkg/bin/pkgsvr";

pub struct PkgfsInstance {
    _system_image_merkle: Hash,
    _process: Scoped<fuchsia_zircon::Process>,
    proxy: DirectoryProxy,
}

impl PkgfsInstance {
    /// Instantiate pkgfs in a sub-process with two inputs: the blobfs root
    /// directory handle and the system image merkle root hash.
    pub fn new(blobfs_root_dir: DirectoryProxy, system_image_merkle: Hash) -> Self {
        let args = vec![
            CString::new(PKGSVR_PATH).unwrap(),
            CString::new(system_image_merkle.to_string().as_bytes()).unwrap(),
        ];
        let argv = args.iter().map(AsRef::as_ref).collect::<Vec<&CStr>>();

        let pkgfs_root_handle_info = HandleInfo::new(HandleType::User0, 0);
        let (proxy, pkgfs_root_server_end) = create_proxy::<DirectoryMarker>().unwrap();

        fx_log_info!("Spawning pkgfs process; binary: {}", PKGSVR_PATH);

        let process = spawn_etc(
            job_default(),
            SpawnOptions::CLONE_ALL,
            &CString::new(PKGSVR_PATH).unwrap(),
            &argv,
            None,
            &mut [
                SpawnAction::add_handle(
                    pkgfs_root_handle_info,
                    pkgfs_root_server_end.into_channel().into(),
                ),
                SpawnAction::add_namespace_entry(
                    &CString::new("/blob").unwrap(),
                    blobfs_root_dir.into_channel().unwrap().into_zx_channel().into(),
                ),
            ],
        )
        .unwrap();

        fx_log_info!("Spawned pkgfs process; binary: {}", PKGSVR_PATH);

        Self { _system_image_merkle: system_image_merkle, _process: process, proxy }
    }

    pub fn proxy(&self) -> DirectoryProxy {
        let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();
        let server_end = server_end.into_channel().into();
        self.proxy.clone(CLONE_FLAG_SAME_RIGHTS, server_end).unwrap();
        proxy
    }
}
