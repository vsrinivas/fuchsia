// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_side_testing::*,
    failure::{Error, ResultExt},
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

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

    let testing = ComponentSideTesting::new()?;

    // Register breakpoints for relevant events
    testing
        .register_breakpoints(vec![
            fbreak::EventType::StopInstance,
            fbreak::EventType::PreDestroyInstance,
            fbreak::EventType::PostDestroyInstance,
        ])
        .await?;

    // Read the children of this component and pass the results to the integration test
    // via HubReport.
    testing.report_directory_contents("/hub/children").await?;

    // Read the hub of the dynamic child and pass the results to the integration test
    // via HubReport
    testing.report_directory_contents("/hub/children/coll:simple_instance").await?;

    // Read the instance id of the dynamic child and pass the results to the integration test
    // via HubReport
    testing.report_file_content("/hub/children/coll:simple_instance/id").await?;

    // Read the children of the dynamic child and pass the results to the integration test
    // via HubReport
    testing.report_directory_contents("/hub/children/coll:simple_instance/children").await?;

    // Bind to the dynamic child
    let mut child_ref = fsys::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };
    let (_dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm.bind_child(&mut child_ref, server_end).await?.expect("failed to bind to child");

    // Read the hub of the dynamic child and pass the results to the integration test
    // via HubReport
    testing.report_directory_contents("/hub/children/coll:simple_instance").await?;

    // Read the children of the dynamic child and pass the results to the integration test
    // via HubReport
    testing.report_directory_contents("/hub/children/coll:simple_instance/children").await?;

    // Read the instance id of the dynamic child's static child and pass the results to the
    // integration test via HubReport
    testing.report_file_content("/hub/children/coll:simple_instance/children/child/id").await?;

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
    testing
        .expect_invocation(fbreak::EventType::PreDestroyInstance, vec!["coll:simple_instance:1"])
        .await?;
    testing.report_directory_contents("/hub/children").await?;
    testing.report_directory_contents("/hub/deleting").await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1").await?;
    testing.resume_invocation().await?;

    // Wait for the dynamic child to stop
    testing
        .expect_invocation(fbreak::EventType::StopInstance, vec!["coll:simple_instance:1"])
        .await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1").await?;
    testing.resume_invocation().await?;

    // Wait for the dynamic child's static child to begin deletion
    testing
        .expect_invocation(
            fbreak::EventType::PreDestroyInstance,
            vec!["coll:simple_instance:1", "child:0"],
        )
        .await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1/children").await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting").await?;
    testing
        .report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting/child:0")
        .await?;
    testing.resume_invocation().await?;

    // Wait for the dynamic child's static child to be destroyed
    testing
        .expect_invocation(
            fbreak::EventType::PostDestroyInstance,
            vec!["coll:simple_instance:1", "child:0"],
        )
        .await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting").await?;
    testing.resume_invocation().await?;

    // Wait for the dynamic child to be destroyed
    testing
        .expect_invocation(fbreak::EventType::PostDestroyInstance, vec!["coll:simple_instance:1"])
        .await?;
    testing.report_directory_contents("/hub/deleting").await?;
    testing.resume_invocation().await?;

    Ok(())
}
