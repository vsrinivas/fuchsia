// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let hub_report =
        connect_to_service::<fhub::HubReportMarker>().context("error connecting to HubReport")?;

    // Read the listing of entires of the hub rooted at this component and
    // pass the results to the integration test via HubReport.
    let hub_proxy = io_util::open_directory_in_namespace(
        "/hub",
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
    .expect("Unable to open Hub.");
    let directory_listing = files_async::readdir(&hub_proxy).await.expect("readdir failed");
    hub_report
        .list_directory("/hub", &mut directory_listing.iter().map(|entry| &entry.name as &str))
        .context("list directory failed")?;

    // Read the listing of the children of the parent from its hub, and pass the
    // results to the integration test via HubReport.
    let parent_hub_children_proxy = io_util::open_directory_in_namespace(
        "/parent_hub/children",
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
    .expect("Unable to open the parent Hub.");
    let parent_hub_children_directory_listing =
        files_async::readdir(&parent_hub_children_proxy).await.expect("readdir failed");
    hub_report
        .list_directory(
            "/parent_hub/children",
            &mut parent_hub_children_directory_listing.iter().map(|entry| &entry.name as &str),
        )
        .context("list directory failed")?;

    // Read the content of the resolved_url file in the sibling hub, and pass the
    // results to the integration test via HubReport.
    let resolved_url_proxy = io_util::open_file_in_namespace(
        "/sibling_hub/exec/resolved_url",
        io_util::OPEN_RIGHT_READABLE,
    )
    .expect("Unable to open the sibling Hub.");
    let resolved_url_file_content =
        io_util::read_file(&resolved_url_proxy).await.expect("readdir failed");
    hub_report
        .report_file_content("/sibling_hub/exec/resolved_url", &resolved_url_file_content)
        .context("list directory failed")?;

    Ok(())
}
