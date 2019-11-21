// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    directory_broker,
    failure::Error,
    fidl::endpoints::*,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::*,
    fuchsia_runtime,
    fuchsia_vfs_pseudo_fs::{directory::entry::DirectoryEntry, pseudo_directory},
    fuchsia_zircon::{self as zx},
    io_util,
    std::iter,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Setup pkgfs at the out directory of this component
    let startup_handle =
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest.into())
            .expect("missing directory request handle");

    let startup_handle = ServerEnd::new(zx::Channel::from(startup_handle));

    let pkg_dir = io_util::open_directory_in_namespace("/pkg", fio::OPEN_RIGHT_READABLE)
        .expect("failed to open /pkg");
    let mut fake_pkgfs = pseudo_directory! {
        "pkgfs" => pseudo_directory! {
            "packages" => pseudo_directory! {
                "echo_server" => pseudo_directory! {
                    "0" => directory_broker::DirectoryBroker::from_directory_proxy(pkg_dir),
                },
            },
        },
    };
    fake_pkgfs.open(
        fio::OPEN_RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        &mut iter::empty(),
        startup_handle,
    );

    // Serve the pkgfs directory
    fasync::spawn(async {
        let _ = fake_pkgfs.await;
        panic!("fake_pkgfs exited!");
    });

    // Bind to the echo_server.
    let mut child_ref = fsys::ChildRef { name: "echo_server".to_string(), collection: None };
    let (_dir_proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
    let realm_proxy = connect_to_service::<fsys::RealmMarker>()?;
    realm_proxy.bind_child(&mut child_ref, server_end).await?.expect("could not bind to child");

    // Wait indefinitely
    fasync::futures::future::pending::<()>().await;
    panic!("This is an unreachable statement!");
}
