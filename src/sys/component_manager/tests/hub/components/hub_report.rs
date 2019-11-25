// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_test_hub as fhub,
    fuchsia_component::client::connect_to_service,
};

macro_rules! get_names_from_listing {
    ($dir_listing:ident) => {
        &mut $dir_listing.iter().map(|entry| &entry.name as &str)
    };
}

/// Connects to HubReport service and provides convenience methods.
pub struct HubReport {
    hub_report: fhub::HubReportProxy,
}

impl HubReport {
    pub fn new() -> Result<Self, Error> {
        let hub_report = connect_to_service::<fhub::HubReportMarker>()
            .context("error connecting to HubReport")?;
        Ok(Self { hub_report })
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
}
