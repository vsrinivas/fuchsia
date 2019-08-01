// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE},
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    std::path::Path,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let hub_report =
        connect_to_service::<fhub::HubReportMarker>().context("error connecting to HubReport")?;

    // Reading the listing of entires of the hub rooted at this component and
    // pass the results to the integration test via HubReport.
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

    let directory_listing = await!(files_async::readdir(&hub_proxy)).expect("readdir failed");
    hub_report
        .list_directory("/hub", &mut directory_listing.iter().map(|entry| &entry.name as &str))
        .context("list directory failed")?;

    // Read the listing of the children of the parent from its hub, and pass the
    // results to the integration test via HubReport.
    let parent_hub_chan = io_util::open_directory_in_namespace(
        "/parent_hub",
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
    .unwrap()
    .into_channel()
    .unwrap()
    .into_zx_channel();
    let parent_hub_proxy = ClientEnd::<DirectoryMarker>::new(parent_hub_chan)
        .into_proxy()
        .expect("Failed to create directory proxy");
    let parent_hub_children_proxy =
        io_util::open_directory(&parent_hub_proxy, &Path::new("children"), OPEN_RIGHT_READABLE)
            .expect("Failed to open directory");
    let parent_hub_children_directory_listing =
        await!(files_async::readdir(&parent_hub_children_proxy)).expect("readdir failed");
    hub_report
        .list_directory(
            "/parent_hub/children",
            &mut parent_hub_children_directory_listing.iter().map(|entry| &entry.name as &str),
        )
        .context("list directory failed")?;

    Ok(())
}
