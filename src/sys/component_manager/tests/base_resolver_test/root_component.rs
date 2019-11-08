// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    directory_broker,
    failure::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_runtime,
    fuchsia_vfs_pseudo_fs::{directory::entry::DirectoryEntry, pseudo_directory},
    fuchsia_zircon::{self as zx},
    io_util,
    std::iter,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
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
    let _ = fake_pkgfs.await;
    panic!("fake_pkgfs exited!");
}
