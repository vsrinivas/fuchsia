// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![recursion_limit = "128"]

use {
    failure::{format_err, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon as zx,
    io_util::{self, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    std::path::PathBuf,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["storage_realm"])?;
    fx_log_info!("storage_realm is starting up");

    let realm_proxy = connect_to_service::<fsys::RealmMarker>()?;

    let (child_data_proxy, server_end) = create_proxy::<fio::DirectoryMarker>()?;
    let mut child_ref = fsys::ChildRef { name: "storage_user".to_string(), collection: None };
    realm_proxy.bind_child(&mut child_ref, server_end).await?
        .map_err(|e| format_err!("failed to bind: {:?}", e))?;
    let child_data_proxy = io_util::open_directory(
        &child_data_proxy,
        &PathBuf::from("data"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )?;

    fx_log_info!("successfully bound to child \"storage_user\"");

    let file_name = "hippo";
    let file_contents = "hippos_are_neat";

    let file = io_util::open_file(
        &child_data_proxy,
        &PathBuf::from(file_name),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
    )?;
    let (s, _) = file.write(&mut file_contents.as_bytes().to_vec().into_iter()).await?;
    assert_eq!(zx::Status::OK, zx::Status::from_raw(s), "writing to the file failed");

    fx_log_info!("successfully wrote to file \"hippo\" in child's outgoing directory");

    let (memfs_proxy, server_end) = create_proxy::<fio::DirectoryMarker>()?;
    let mut child_ref = fsys::ChildRef { name: "memfs".to_string(), collection: None };
    realm_proxy.bind_child(&mut child_ref, server_end).await?
        .map_err(|e| format_err!("failed to bind: {:?}", e))?;
    let memfs_proxy = io_util::open_directory(
        &memfs_proxy,
        &PathBuf::from("memfs"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )?;

    fx_log_info!("successfully bound to child \"memfs\"");

    let file_proxy = io_util::open_file(
        &memfs_proxy,
        &PathBuf::from("storage_user/data/hippo"),
        OPEN_RIGHT_READABLE,
    )?;
    let read_contents = io_util::read_file(&file_proxy).await?;

    fx_log_info!("successfully read back contents of file from memfs directly");
    assert_eq!(read_contents, file_contents, "file contents did not match what was written");

    println!("Test passes");
    Ok(())
}
