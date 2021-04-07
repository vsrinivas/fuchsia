// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, FileMarker, FileProxy},
};

// Utility function to open a new node connection under |dir|.
// Does not validate the node connection after opening.
pub fn open_file(
    dir: &DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> Result<FileProxy, Error> {
    let (client_end, server_end) = create_proxy::<FileMarker>().expect("create_proxy failed");
    dir.open(flags, mode, path, ServerEnd::new(server_end.into_channel()))?;
    Ok(client_end)
}

// Utility function to open a new node connection under |dir|.
// Validates the node connection after opening by calling |describe|.
pub async fn open_file_validating(
    dir: &DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> Result<FileProxy, Error> {
    let client_end = open_file(dir, flags, mode, path)?;
    client_end.describe().await?;
    Ok(client_end)
}

// Utility function to open a new node connection under |dir|.
// Does not validate the node connection after opening.
pub fn open_dir(
    dir: &DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> Result<DirectoryProxy, Error> {
    let (client_end, server_end) = create_proxy::<DirectoryMarker>().expect("create_proxy failed");
    dir.open(flags, mode, path, ServerEnd::new(server_end.into_channel()))?;
    Ok(client_end)
}

// Utility function to open a new node connection under |dir|.
// Validates the node connection after opening by calling |describe|.
pub async fn open_dir_validating(
    dir: &DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> Result<DirectoryProxy, Error> {
    let client_end = open_dir(dir, flags, mode, path)?;
    client_end.describe().await?;
    Ok(client_end)
}
