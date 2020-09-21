// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _, fidl::endpoints, fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service, futures::prelude::*, hub_report::HubReport,
    test_utils_lib::events::*,
};

#[fasync::run_singlethreaded]
async fn main() {
    let event_source = EventSource::new_sync().unwrap();

    // Subscribe to relevant events
    let mut event_stream = event_source
        .subscribe(vec![Stopped::NAME, MarkedForDestruction::NAME, Destroyed::NAME])
        .await
        .unwrap();

    // Creating children will not complete until `start_component_tree` is called.
    event_source.start_component_tree().await;

    // Create a dynamic child component
    let realm =
        connect_to_service::<fsys::RealmMarker>().context("error connecting to realm").unwrap();
    let mut collection_ref = fsys::CollectionRef { name: String::from("coll") };
    let child_decl = fsys::ChildDecl {
        name: Some(String::from("simple_instance")),
        url: Some(String::from("fuchsia-pkg://fuchsia.com/hub_integration_test#meta/simple.cm")),
        startup: Some(fsys::StartupMode::Lazy),
        environment: None,
    };
    realm
        .create_child(&mut collection_ref, child_decl)
        .await
        .context("create_child failed")
        .unwrap()
        .expect("failed to create child");

    let hub_report = HubReport::new().unwrap();

    // Read the children of this component and pass the results to the integration test
    // via HubReport.
    hub_report.report_directory_contents("/hub/children").await.unwrap();

    // Read the hub of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report.report_directory_contents("/hub/children/coll:simple_instance").await.unwrap();

    // Read the instance id of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report.report_file_content("/hub/children/coll:simple_instance/id").await.unwrap();

    // Read the children of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report
        .report_directory_contents("/hub/children/coll:simple_instance/children")
        .await
        .unwrap();

    // Bind to the dynamic child
    let mut child_ref = fsys::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };
    let (_dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm.bind_child(&mut child_ref, server_end).await.unwrap().expect("failed to bind to child");

    // Read the hub of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report.report_directory_contents("/hub/children/coll:simple_instance").await.unwrap();

    // Read the children of the dynamic child and pass the results to the integration test
    // via HubReport
    hub_report
        .report_directory_contents("/hub/children/coll:simple_instance/children")
        .await
        .unwrap();

    // Read the instance id of the dynamic child's static child and pass the results to the
    // integration test via HubReport
    hub_report
        .report_file_content("/hub/children/coll:simple_instance/children/child/id")
        .await
        .unwrap();

    // Delete the dynamic child
    let mut child_ref = fsys::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };
    let (f, destroy_handle) = realm.destroy_child(&mut child_ref).remote_handle();
    fasync::Task::spawn(f).detach();

    // Wait for the dynamic child to begin deletion
    let event = event_stream
        .expect_exact::<MarkedForDestruction>(
            EventMatcher::new().expect_moniker("./coll:simple_instance:1"),
        )
        .await;
    hub_report.report_directory_contents("/hub/children").await.unwrap();
    hub_report.report_directory_contents("/hub/deleting").await.unwrap();
    hub_report.report_directory_contents("/hub/deleting/coll:simple_instance:1").await.unwrap();
    event.resume().await.unwrap();

    // Wait for the destroy call to return
    destroy_handle.await.context("delete_child failed").unwrap().expect("failed to delete child");

    // Wait for the dynamic child to stop
    let event = event_stream
        .expect_exact::<Stopped>(EventMatcher::new().expect_moniker("./coll:simple_instance:1"))
        .await;
    hub_report.report_directory_contents("/hub/deleting/coll:simple_instance:1").await.unwrap();
    event.resume().await.unwrap();

    // Wait for the dynamic child's static child to begin deletion
    let event = event_stream
        .expect_exact::<MarkedForDestruction>(
            EventMatcher::new().expect_moniker("./coll:simple_instance:1/child:0"),
        )
        .await;
    hub_report
        .report_directory_contents("/hub/deleting/coll:simple_instance:1/children")
        .await
        .unwrap();
    hub_report
        .report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting")
        .await
        .unwrap();
    hub_report
        .report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting/child:0")
        .await
        .unwrap();
    event.resume().await.unwrap();

    // Wait for the dynamic child's static child to be destroyed
    let event = event_stream
        .expect_exact::<Destroyed>(
            EventMatcher::new().expect_moniker("./coll:simple_instance:1/child:0"),
        )
        .await;
    hub_report
        .report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting")
        .await
        .unwrap();
    event.resume().await.unwrap();

    // Wait for the dynamic child to be destroyed
    let event = event_stream
        .expect_exact::<Destroyed>(EventMatcher::new().expect_moniker("./coll:simple_instance:1"))
        .await;
    hub_report.report_directory_contents("/hub/deleting").await.unwrap();
    event.resume().await.unwrap();
}
