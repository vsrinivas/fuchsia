// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_test_breakpoints as fbreak, fidl_fuchsia_test_hub as fhub,
    fuchsia_component::client::connect_to_service,
};

macro_rules! get_names_from_listing {
    ($dir_listing:ident) => {
        &mut $dir_listing.iter().map(|entry| &entry.name as &str)
    };
}

/// Connects to testing services and provides convenience methods.
/// Used by components in integration tests.
pub struct ComponentSideTesting {
    hub_report: fhub::HubReportProxy,
    breakpoints: fbreak::BreakpointsProxy,
}

impl ComponentSideTesting {
    pub fn new() -> Result<Self, Error> {
        let hub_report = connect_to_service::<fhub::HubReportMarker>()
            .context("error connecting to HubReport")?;
        let breakpoints = connect_to_service::<fbreak::BreakpointsMarker>()
            .context("error connecting to Breakpoints")?;
        Ok(Self { hub_report, breakpoints })
    }

    /// Report the contents of a directory to the integration test.
    /// This method blocks until the integration test is ready to accept the contents.
    pub async fn report_directory_contents(&self, dir_path: &str) -> Result<(), Error> {
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

    /// Report the contents of a file to the integration test.
    /// This method blocks until the integration test is ready to accept the contents.
    pub async fn report_file_content(&self, path: &str) -> Result<(), Error> {
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

    /// Register breakpoints with the ComponentManager.
    /// Requires the integration test to setup a BreakpointRegistry and attach
    /// the BreakpointCapabilityHook.
    pub async fn register_breakpoints(
        &self,
        event_types: Vec<fbreak::EventType>,
    ) -> Result<(), Error> {
        self.breakpoints
            .register(&mut event_types.into_iter())
            .await
            .context("register breakpoints failed")?;
        Ok(())
    }

    /// Blocks until the next invocation of a breakpoint.
    /// If the invocation is not the expected type or against the expected component,
    /// a panic will occur.
    pub async fn expect_invocation(
        &self,
        event_type: fbreak::EventType,
        component: Vec<&str>,
    ) -> Result<(), Error> {
        self.breakpoints
            .expect(event_type, &mut component.into_iter())
            .await
            .context("expect invocation failed")?;
        Ok(())
    }

    /// Blocks until a UseCapability invocation matching the specified component
    /// and capability basename. All other invocations are ignored.
    pub async fn wait_until_use_capability(
        &self,
        components: Vec<&str>,
        capability_basename: &str,
    ) -> Result<(), Error> {
        self.breakpoints
            .wait_until_use_capability(&mut components.into_iter(), capability_basename)
            .await
            .context("wait until use capability failed")?;
        Ok(())
    }

    /// Resume the component manager from the last expected invocation.
    pub async fn resume_invocation(&self) -> Result<(), Error> {
        self.breakpoints.resume().await.context("resume invocation failed")?;
        Ok(())
    }
}
