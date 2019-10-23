// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
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
        .await
        .context("list directory failed")?;
    Ok(())
}

async fn report_file_content(hub_report: &fhub::HubReportProxy, path: &str) -> Result<(), Error> {
    let resolved_url_proxy = io_util::open_file_in_namespace(path, io_util::OPEN_RIGHT_READABLE)
        .expect("Unable to open the file.");
    let resolved_url_file_content = io_util::read_file(&resolved_url_proxy).await?;
    hub_report
        .report_file_content(path, &resolved_url_file_content)
        .await
        .context("report file content failed")?;
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
    report_directory_contents(&hub_report, "/hub/children/coll:simple_instance").await?;

    // Read the instance id of the dynamic child and pass the results to the integration test
    // via HubReport
    report_file_content(&hub_report, "/hub/children/coll:simple_instance/id").await?;

    // Read the children of the dynamic child and pass the results to the integration test
    // via HubReport
    report_directory_contents(&hub_report, "/hub/children/coll:simple_instance/children").await?;

    // Bind to the dynamic child
    let mut child_ref = fsys::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };
    let (_dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm.bind_child(&mut child_ref, server_end).await?.expect("failed to bind to child");

    // Read the hub of the dynamic child and pass the results to the integration test
    // via HubReport
    report_directory_contents(&hub_report, "/hub/children/coll:simple_instance").await?;

    // Read the children of the dynamic child and pass the results to the integration test
    // via HubReport
    report_directory_contents(&hub_report, "/hub/children/coll:simple_instance/children").await?;

    // Read the instance id of the dynamic child's static child and pass the results to the
    // integration test via HubReport
    report_file_content(&hub_report, "/hub/children/coll:simple_instance/children/child/id").await?;

    // Delete the dynamic child
    let mut child_ref = fsys::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };
    realm
        .destroy_child(&mut child_ref)
        .await
        .context("delete_child failed")?
        .expect("failed to delete child");

    // TODO(xbhatnag): Add breakpointing for component side so that directory contents can be
    //                 reported after stop/deletion.

    Ok(())
}
