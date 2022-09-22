// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    component_hub::capability,
    ffx_core::ffx_plugin,
    ffx_driver_args::DriverCommand,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_device_manager as fdm,
    fidl_fuchsia_driver_development as fdd, fidl_fuchsia_driver_playground as fdp,
    fidl_fuchsia_driver_registrar as fdr, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fidl_fuchsia_test_manager as ftm,
    fuchsia_zircon_status::Status,
    selectors::{self, VerboseError},
};

struct DriverConnector {
    remote_control: Option<rc::RemoteControlProxy>,
}

impl DriverConnector {
    fn new(remote_control: Option<rc::RemoteControlProxy>) -> Self {
        Self { remote_control }
    }

    async fn get_component_with_capability<S: ProtocolMarker>(
        &self,
        capability: &str,
        default_selector: &str,
        select: bool,
    ) -> Result<S::Proxy> {
        async fn remotecontrol_connect<S: ProtocolMarker>(
            remote_control: &rc::RemoteControlProxy,
            selector: &str,
        ) -> Result<S::Proxy> {
            let (proxy, server_end) = fidl::endpoints::create_proxy::<S>()
                .with_context(|| format!("failed to create proxy to {}", S::DEBUG_NAME))?;
            let _: rc::ServiceMatch = remote_control
                .connect(
                    selectors::parse_selector::<VerboseError>(selector)?,
                    server_end.into_channel(),
                )
                .await?
                .map_err(|e| {
                    anyhow::anyhow!(
                        "failed to connect to {} as {}: {:?}",
                        S::DEBUG_NAME,
                        selector,
                        e
                    )
                })?;
            Ok(proxy)
        }

        async fn find_components_with_capability(
            rcs_proxy: &rc::RemoteControlProxy,
            capability: &str,
        ) -> Result<Vec<String>> {
            let (query_proxy, query_server) =
                fidl::endpoints::create_proxy::<fsys::RealmQueryMarker>()
                    .context("creating realm query proxy")?;
            let (explorer_proxy, explorer_server) =
                fidl::endpoints::create_proxy::<fsys::RealmExplorerMarker>()
                    .context("creating realm explorer proxy")?;
            rcs_proxy
                .root_realm_explorer(explorer_server)
                .await?
                .map_err(|i| Status::ok(i).unwrap_err())
                .context("opening explorer")?;
            rcs_proxy
                .root_realm_query(query_server)
                .await?
                .map_err(|i| Status::ok(i).unwrap_err())
                .context("opening query")?;

            Ok(capability::find_instances_that_expose_or_use_capability(
                capability.to_string(),
                &explorer_proxy,
                &query_proxy,
            )
            .await?
            .exposed
            .iter()
            .map(|c| c.to_string().split_off(1))
            .collect())
        }

        /// Find the components that expose a given capability, and let the user
        /// request which component they would like to connect to.
        async fn user_choose_selector(
            remote_control: &rc::RemoteControlProxy,
            capability: &str,
        ) -> Result<String> {
            let capabilities = find_components_with_capability(&remote_control, capability).await?;
            println!("Please choose which component to connect to:");
            for (i, component) in capabilities.iter().enumerate() {
                println!("    {}: {}", i, component)
            }

            let mut line_editor = rustyline::Editor::<()>::new();
            loop {
                let line = line_editor.readline("$ ")?;
                let choice = line.trim().parse::<usize>();
                if choice.is_err() {
                    println!("Error: please choose a value.");
                    continue;
                }
                let choice = choice.unwrap();
                if choice >= capabilities.len() {
                    println!("Error: please choose a correct value.");
                    continue;
                }
                // TODO(fxbug.dev/85516): We have to replace ':' with '*' because they are parsed
                // incorrectly in a selector.
                return Ok(capabilities[choice].replace(":", "*") + ":expose:" + capability);
            }
        }

        if let Some(ref remote_control) = self.remote_control {
            let selector = match select {
                true => user_choose_selector(remote_control, capability).await?,
                false => default_selector.to_string(),
            };
            remotecontrol_connect::<S>(&remote_control, &selector).await
        } else {
            anyhow::bail!("Failed to get remote control proxy");
        }
    }
}

#[async_trait::async_trait]
impl driver_connector::DriverConnector for DriverConnector {
    async fn get_driver_development_proxy(
        &self,
        select: bool,
    ) -> Result<fdd::DriverDevelopmentProxy> {
        self.get_component_with_capability::<fdd::DriverDevelopmentMarker>(
            "fuchsia.driver.development.DriverDevelopment",
            "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment",
            select,
        )
        .await
        .context("Failed to get driver development component")
    }

    async fn get_dev_proxy(&self, select: bool) -> Result<fio::DirectoryProxy> {
        self.get_component_with_capability::<fio::DirectoryMarker>(
            "dev",
            "bootstrap/driver_manager:expose:dev",
            select,
        )
        .await
        .context("Failed to get dev component")
    }

    async fn get_device_watcher_proxy(&self) -> Result<fdm::DeviceWatcherProxy> {
        self.get_component_with_capability::<fdm::DeviceWatcherMarker>(
            "fuchsia.hardware.usb.DeviceWatcher",
            "bootstrap/driver_manager:expose:fuchsia.hardware.usb.DeviceWatcher",
            false,
        )
        .await
        .context("Failed to get device watcher component")
    }

    async fn get_driver_registrar_proxy(&self, select: bool) -> Result<fdr::DriverRegistrarProxy> {
        self.get_component_with_capability::<fdr::DriverRegistrarMarker>(
            "fuchsia.driver.registrar.DriverRegistrar",
            "bootstrap/driver_index:expose:fuchsia.driver.registrar.DriverRegistrar",
            select,
        )
        .await
        .context("Failed to get driver registrar component")
    }

    async fn get_tool_runner_proxy(&self, select: bool) -> Result<fdp::ToolRunnerProxy> {
        self.get_component_with_capability::<fdp::ToolRunnerMarker>(
            "fuchsia.driver.playground.ToolRunner",
            "core/driver_playground:expose:fuchsia.driver.playground.ToolRunner",
            select,
        )
        .await
        .context("Failed to get tool runner component")
    }

    async fn get_run_builder_proxy(&self) -> Result<ftm::RunBuilderProxy> {
        self.get_component_with_capability::<ftm::RunBuilderMarker>(
            "",
            "core/test_manager:expose:fuchsia.test.manager.RunBuilder",
            false,
        )
        .await
        .context("Failed to get RunBuilder component")
    }
}

#[ffx_plugin()]
pub async fn driver(
    remote_control: Option<rc::RemoteControlProxy>,
    cmd: DriverCommand,
) -> Result<()> {
    driver_tools::driver(cmd.into(), DriverConnector::new(remote_control)).await
}
