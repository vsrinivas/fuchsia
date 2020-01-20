// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    breakpoint_system_client::*,
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    hub_report::HubReport,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let breakpoint_system = BreakpointSystemClient::new()?;
    // Creating children will not complete until `start_component_tree` is called.
    breakpoint_system.start_component_tree().await?;

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

    let hub_report = HubReport::new()?;

    // Read the children of this component and pass the results to the integration test
    // via HubReport.
    hub_report.report_directory_contents("/hub/children").await?;

    // Read the hub of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report.report_directory_contents("/hub/children/coll:simple_instance").await?;

    // Read the instance id of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report.report_file_content("/hub/children/coll:simple_instance/id").await?;

    // Read the children of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report.report_directory_contents("/hub/children/coll:simple_instance/children").await?;

    // Bind to the dynamic child
    let mut child_ref = fsys::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };
    let (_dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm.bind_child(&mut child_ref, server_end).await?.expect("failed to bind to child");

    // Read the hub of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report.report_directory_contents("/hub/children/coll:simple_instance").await?;

    // Read the children of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report.report_directory_contents("/hub/children/coll:simple_instance/children").await?;

    // Read the instance id of the dynamic child's static child and pass the results to the
    // integration test via HubReport
    hub_report.report_file_content("/hub/children/coll:simple_instance/children/child/id").await?;

    // Register breakpoints for relevant events
    let receiver = breakpoint_system
        .set_breakpoints(vec![
            StopInstance::TYPE,
            PreDestroyInstance::TYPE,
            PostDestroyInstance::TYPE,
        ])
        .await?;

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

    // Wait for the dynamic child to begin deletion
    let invocation =
        receiver.expect_exact::<PreDestroyInstance>("./coll:simple_instance:1").await?;
    hub_report.report_directory_contents("/hub/children").await?;
    hub_report.report_directory_contents("/hub/deleting").await?;
    hub_report.report_directory_contents("/hub/deleting/coll:simple_instance:1").await?;
    invocation.resume().await?;

    // Wait for the dynamic child to stop
    let invocation = receiver.expect_exact::<StopInstance>("./coll:simple_instance:1").await?;
    hub_report.report_directory_contents("/hub/deleting/coll:simple_instance:1").await?;
    invocation.resume().await?;

    // Wait for the dynamic child's static child to begin deletion
    let invocation =
        receiver.expect_exact::<PreDestroyInstance>("./coll:simple_instance:1/child:0").await?;
    hub_report.report_directory_contents("/hub/deleting/coll:simple_instance:1/children").await?;
    hub_report.report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting").await?;
    hub_report
        .report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting/child:0")
        .await?;
    invocation.resume().await?;

    // Wait for the dynamic child's static child to be destroyed
    let invocation =
        receiver.expect_exact::<PostDestroyInstance>("./coll:simple_instance:1/child:0").await?;
    hub_report.report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting").await?;
    invocation.resume().await?;

    // Wait for the dynamic child to be destroyed
    let invocation =
        receiver.expect_exact::<PostDestroyInstance>("./coll:simple_instance:1").await?;
    hub_report.report_directory_contents("/hub/deleting").await?;
    invocation.resume().await?;

    Ok(())
}
