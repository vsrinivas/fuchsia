// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

macro_rules! get_names_from_listing {
    ($dir_listing:ident) => {
        &mut $dir_listing.iter().map(|entry| &entry.name as &str)
    };
}

async fn report_directory_contents(
    hub_report: &fhub::HubReportProxy,
    dir_path: &str,
) -> Result<(), Error> {
    let dir_proxy = io_util::open_directory_in_namespace(dir_path, io_util::OPEN_RIGHT_READABLE)
        .expect("Unable to open directory in namespace");
    let dir_listing = files_async::readdir(&dir_proxy).await.expect("readdir failed");
    hub_report
        .list_directory(dir_path, get_names_from_listing!(dir_listing))
        .await
        .context("list directory failed")?;
    Ok(())
}

async fn report_file_content(hub_report: &fhub::HubReportProxy, path: &str) -> Result<(), Error> {
    let resolved_url_proxy = io_util::open_file_in_namespace(path, io_util::OPEN_RIGHT_READABLE)
        .expect("Unable to open the sibling Hub.");
    let resolved_url_file_content = io_util::read_file(&resolved_url_proxy).await?;
    hub_report
        .report_file_content(path, &resolved_url_file_content)
        .await
        .context("report file content failed")?;
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let hub_report =
        connect_to_service::<fhub::HubReportMarker>().context("error connecting to HubReport")?;

    // Read the listing of entires of the hub rooted at this component and
    // pass the results to the integration test via HubReport.
    report_directory_contents(&hub_report, "/hub").await?;

    // Read the listing of the children of the parent from its hub, and pass the
    // results to the integration test via HubReport.
    report_directory_contents(&hub_report, "/parent_hub/children").await?;

    // Read the content of the resolved_url file in the sibling hub, and pass the
    // results to the integration test via HubReport.
    report_file_content(&hub_report, "/sibling_hub/exec/resolved_url").await?;

    Ok(())
}
