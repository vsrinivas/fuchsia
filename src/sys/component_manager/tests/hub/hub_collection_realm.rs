// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync,
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
    // Create a dynamic child component
    let realm = connect_to_service::<fsys::RealmMarker>().context("error connecting to realm")?;
    let mut collection_ref = fsys::CollectionRef { name: String::from("coll") };
    let child_decl = fsys::ChildDecl {
        name: Some(String::from("simple_instance")),
        url: Some(String::from("fuchsia-pkg://fuchsia.com/hub_integration_test#meta/simple.cm")),
        startup: Some(fsys::StartupMode::Lazy),
    };
    realm
        .create_child(&mut collection_ref, child_decl)
        .await
        .context("create_child failed")?
        .expect("failed to create child");

    let hub_report =
        connect_to_service::<fhub::HubReportMarker>().context("error connecting to HubReport")?;

    // Read the children of this component and pass the results to the integration test
    // via HubReport.
    report_directory_contents(&hub_report, "/hub/children").await?;

    // Read the hub of the dynamic child and pass the results to the integration test
    // via HubReport
    report_directory_contents(&hub_report, "/hub/children/coll:simple_instance:1").await?;

    Ok(())
}
