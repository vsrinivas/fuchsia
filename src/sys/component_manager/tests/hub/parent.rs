// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

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
        .context("list directory failed")?;
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let hub_report =
        connect_to_service::<fhub::HubReportMarker>().context("error connecting to HubReport")?;

    // Read the children of this component and pass the results to the integration test
    // via HubReport.
    report_directory_contents(&hub_report, "/hub/children").await?;

    // Read the hub of the child and pass the results to the integration test
    // via HubReport
    report_directory_contents(&hub_report, "/hub/children/child:0").await?;

    // Read the grandchildren and pass the results to the integration test
    // via HubReport
    report_directory_contents(&hub_report, "/hub/children/child:0/children").await?;

    Ok(())
}
