// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_openthread_devmgr::IsolatedDevmgrMarker,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
    std::{fs::File, path::Path},
};

/// Opens a path
pub fn open_in_isolated_devmgr(path: &str, flags: u32) -> Result<File, zx::Status> {
    let isolated_devmgr =
        connect_to_service::<IsolatedDevmgrMarker>().expect("connecting to isolated devmgr.");
    let (node_proxy, server_end) =
        fidl::endpoints::create_endpoints().expect("creating channel for devfs node");
    println!("isolated_devmgr opening file...");
    isolated_devmgr
        .open(flags, 0, path, server_end)
        .expect("opening devfs node in isolated devmgr");
    println!("done");
    fdio::create_fd(node_proxy.into_channel().into())
}

/// Opens a path as a directory
pub fn open_dir_in_isolated_devmgr<P: AsRef<Path>>(path: P) -> Result<File, zx::Status> {
    let flags = fidl_fuchsia_io::OPEN_FLAG_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE;
    open_in_isolated_devmgr(path.as_ref().to_str().unwrap(), flags)
}

/// Opens a path as a file
pub fn open_file_in_isolated_devmgr<P: AsRef<Path>>(path: P) -> Result<File, zx::Status> {
    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;
    open_in_isolated_devmgr(path.as_ref().to_str().unwrap(), flags)
}
