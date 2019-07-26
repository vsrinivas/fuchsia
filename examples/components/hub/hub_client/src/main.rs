// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let hub_chan = io_util::open_directory_in_namespace(
        "/hub",
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
    .unwrap()
    .into_channel()
    .unwrap()
    .into_zx_channel();
    let hub_proxy = ClientEnd::<DirectoryMarker>::new(hub_chan)
        .into_proxy()
        .expect("Failed to create directory proxy");

    let hub_report =
        connect_to_service::<fhub::HubReportMarker>().context("error connecting to HubReport")?;
    let directory_listing = await!(files_async::readdir(&hub_proxy)).expect("readdir failed");
    hub_report
        .list_directory("/hub", &mut directory_listing.iter().map(|entry| &entry.name as &str))
        .context("list directory failed")?;
    Ok(())
}
