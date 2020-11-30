// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fake_pkgfs::{Entry, MockDir},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    futures::future::pending,
    std::sync::Arc,
};

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init().expect("failed to initialize logging");

    let outgoing_handle =
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest.into())
            .expect("failed to take startup handle");

    let real_pkg = io_util::directory::open_in_namespace(
        "/pkg",
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )
    .expect("failed to open /pkg");
    let fake_pkgfs = Arc::new(MockDir::new().add_entry(
        "pkgfs",
        Arc::new(MockDir::new().add_entry(
            "packages",
            Arc::new(MockDir::new().add_entry(
                "mock-package",
                Arc::new(MockDir::new().add_entry("0", Arc::new(real_pkg))),
            )),
        )),
    ));
    fake_pkgfs.open(
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        fio::MODE_TYPE_DIRECTORY,
        ".",
        ServerEnd::new(outgoing_handle.into()),
    );
    pending::<()>().await;
}
