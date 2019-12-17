// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_developer_remotecontrol as rcs, fidl_fuchsia_device_manager as fdevmgr,
    fuchsia_component::client::{launcher, AppBuilder},
    futures::prelude::*,
    std::convert::TryInto,
};

pub struct RemoteControlService {
    admin_proxy: fdevmgr::AdministratorProxy,
}

impl RemoteControlService {
    pub fn new() -> Result<Self, Error> {
        return Ok(Self::new_with_proxies(Self::construct_admin_proxy()?));
    }

    pub fn new_with_proxies(admin_proxy: fdevmgr::AdministratorProxy) -> Self {
        return Self { admin_proxy };
    }

    pub async fn serve_stream<'a>(
        &'a self,
        mut stream: rcs::FdbRemoteControlRequestStream,
    ) -> Result<(), Error> {
        match stream.try_next().await.context("next RemoteControl request")? {
            Some(rcs::FdbRemoteControlRequest::RunComponent { component_url, args, responder }) => {
                let response = self.spawn_component(&component_url, args).await?;
                responder.send(response).context("sending RunComponent response")?;
            }
            Some(rcs::FdbRemoteControlRequest::RebootDevice { responder }) => {
                self.reboot_device(responder).await?;
            }
            None => {
                log::info!("empty stream!");
            }
        };
        Ok(())
    }

    fn construct_admin_proxy() -> Result<fdevmgr::AdministratorProxy, Error> {
        let proxy = fuchsia_component::client::connect_to_service::<fdevmgr::AdministratorMarker>()
            .map_err(|s| format_err!("Failed to connect to DevMgr service: {}", s))?;
        return Ok(proxy);
    }

    pub async fn spawn_component(
        &self,
        component_name: &str,
        argv: Vec<String>,
    ) -> Result<rcs::RunComponentResponse, Error> {
        log::info!("Attempting to run component '{}' with argv {:?}...", component_name, argv);
        let launcher = launcher().expect("Failed to open launcher service");
        let mut app = AppBuilder::new(component_name);
        app = app.args(argv);

        let output = app.output(&launcher)?.await?;

        let response = rcs::RunComponentResponse {
            component_stdout: Some(String::from_utf8_lossy(&output.stdout).to_string()),
            component_stderr: Some(String::from_utf8_lossy(&output.stderr).to_string()),
            exit_code: Some(output.exit_status.code().try_into().unwrap()),
        };
        Ok(response)
    }

    pub async fn reboot_device<'a>(
        &'a self,
        responder: rcs::FdbRemoteControlRebootDeviceResponder,
    ) -> Result<(), Error> {
        responder.send().context("failed to send successful reboot response.")?;

        log::info!("got remote control request to reboot device.");
        self.admin_proxy.suspend(fdevmgr::SUSPEND_FLAG_REBOOT).await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_developer_remotecontrol as rcs, fuchsia_async as fasync,
        fuchsia_zircon as zx,
    };

    fn setup_fake_admin_service() -> fdevmgr::AdministratorProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fdevmgr::AdministratorMarker>().unwrap();

        fasync::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fdevmgr::AdministratorRequest::Suspend {
                        flags: fdevmgr::SUSPEND_FLAG_REBOOT,
                        responder,
                    }) => {
                        let _ = responder.send(zx::Status::OK.into_raw());
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_spawn_hello_world() -> Result<(), Error> {
        let (rcs_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<rcs::FdbRemoteControlMarker>().unwrap();
        fasync::spawn(async move {
            RemoteControlService::new().unwrap().serve_stream(stream).await.unwrap();
        });

        let argv = vec![];
        let response = rcs_proxy
            .run_component(
                "fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx",
                &mut argv.iter().copied(),
            )
            .await
            .unwrap();

        assert_eq!(response.component_stdout, Some(String::from("Hello, world!\n")));
        assert_eq!(response.component_stderr, Some(String::default()));
        assert_eq!(response.exit_code, Some(0));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reboot() -> Result<(), Error> {
        let (rcs_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<rcs::FdbRemoteControlMarker>().unwrap();
        fasync::spawn(async move {
            RemoteControlService::new_with_proxies(setup_fake_admin_service())
                .serve_stream(stream)
                .await
                .unwrap();
        });

        let response = rcs_proxy.reboot_device().await.unwrap();

        assert_eq!(response, ());
        Ok(())
    }
}
