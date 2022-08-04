// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, fidl_fuchsia_device_manager as fdm, fidl_fuchsia_driver_development as fdd,
    fidl_fuchsia_driver_playground as fdp, fidl_fuchsia_driver_registrar as fdr,
    fidl_fuchsia_io as fio, fidl_fuchsia_test_manager as ftm,
};

#[async_trait::async_trait]
pub trait DriverConnector {
    async fn get_driver_development_proxy(
        &self,
        select: bool,
    ) -> Result<fdd::DriverDevelopmentProxy>;
    async fn get_dev_proxy(&self, select: bool) -> Result<fio::DirectoryProxy>;
    async fn get_device_watcher_proxy(&self) -> Result<fdm::DeviceWatcherProxy>;
    async fn get_driver_registrar_proxy(&self, select: bool) -> Result<fdr::DriverRegistrarProxy>;
    async fn get_tool_runner_proxy(&self, select: bool) -> Result<fdp::ToolRunnerProxy>;
    async fn get_run_builder_proxy(&self) -> Result<ftm::RunBuilderProxy>;
}
