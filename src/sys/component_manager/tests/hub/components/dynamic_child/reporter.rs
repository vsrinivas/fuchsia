// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_breakpoints as fbreak,
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

macro_rules! get_names_from_listing {
    ($dir_listing:ident) => {
        &mut $dir_listing.iter().map(|entry| &entry.name as &str)
    };
}

struct Testing {
    hub_report: fhub::HubReportProxy,
    breakpoints: fbreak::BreakpointsProxy,
}

impl Testing {
    fn new() -> Result<Self, Error> {
        let hub_report = connect_to_service::<fhub::HubReportMarker>()
            .context("error connecting to HubReport")?;
        let breakpoints = connect_to_service::<fbreak::BreakpointsMarker>()
            .context("error connecting to Breakpoints")?;
        Ok(Self { hub_report, breakpoints })
    }

    async fn report_directory_contents(&self, dir_path: &str) -> Result<(), Error> {
        let dir_proxy =
            io_util::open_directory_in_namespace(dir_path, io_util::OPEN_RIGHT_READABLE)
                .expect("Unable to open directory in namespace");
        let dir_listing = files_async::readdir(&dir_proxy).await.expect("readdir failed");
        self.hub_report
            .list_directory(dir_path, get_names_from_listing!(dir_listing))
            .await
            .context("list directory failed")?;
        Ok(())
    }

    async fn register_breakpoints(&self, event_types: Vec<fbreak::EventType>) -> Result<(), Error> {
        self.breakpoints
            .register(&mut event_types.into_iter())
            .await
            .context("register breakpoints failed")?;
        Ok(())
    }

    async fn report_file_content(&self, path: &str) -> Result<(), Error> {
        let resolved_url_proxy =
            io_util::open_file_in_namespace(path, io_util::OPEN_RIGHT_READABLE)
                .expect("Unable to open the file.");
        let resolved_url_file_content = io_util::read_file(&resolved_url_proxy).await?;
        self.hub_report
            .report_file_content(path, &resolved_url_file_content)
            .await
            .context("report file content failed")?;
        Ok(())
    }

    async fn expect_breakpoint(
        &self,
        event_type: fbreak::EventType,
        components: Vec<&str>,
    ) -> Result<(), Error> {
        self.breakpoints
            .expect(event_type, &mut components.into_iter())
            .await
            .context("expect breakpoint failed")?;
        Ok(())
    }

    async fn resume_breakpoint(&self) -> Result<(), Error> {
        self.breakpoints.resume().await.context("resume breakpoint failed")?;
        Ok(())
    }
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

    let testing = Testing::new()?;

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
        .expect_breakpoint(fbreak::EventType::PreDestroyInstance, vec!["coll:simple_instance:1"])
        .await?;
    testing.report_directory_contents("/hub/children").await?;
    testing.report_directory_contents("/hub/deleting").await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1").await?;
    testing.resume_breakpoint().await?;

    // Wait for the dynamic child to stop
    testing
        .expect_breakpoint(fbreak::EventType::StopInstance, vec!["coll:simple_instance:1"])
        .await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1").await?;
    testing.resume_breakpoint().await?;

    // Wait for the dynamic child's static child to begin deletion
    testing
        .expect_breakpoint(
            fbreak::EventType::PreDestroyInstance,
            vec!["coll:simple_instance:1", "child:0"],
        )
        .await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1/children").await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting").await?;
    testing
        .report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting/child:0")
        .await?;
    testing.resume_breakpoint().await?;

    // Wait for the dynamic child's static child to be destroyed
    testing
        .expect_breakpoint(
            fbreak::EventType::PostDestroyInstance,
            vec!["coll:simple_instance:1", "child:0"],
        )
        .await?;
    testing.report_directory_contents("/hub/deleting/coll:simple_instance:1/deleting").await?;
    testing.resume_breakpoint().await?;

    // Wait for the dynamic child to be destroyed
    testing
        .expect_breakpoint(fbreak::EventType::PostDestroyInstance, vec!["coll:simple_instance:1"])
        .await?;
    testing.report_directory_contents("/hub/deleting").await?;
    testing.resume_breakpoint().await?;

    Ok(())
}
