// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_control::ComponentController,
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_developer_remotecontrol as rcs, fidl_fuchsia_device_manager as fdevmgr,
    fuchsia_component::client::{launcher, AppBuilder},
    futures::prelude::*,
    std::convert::TryInto,
};

mod component_control;

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
        while let Some(request) = stream.try_next().await.context("next RemoteControl request")? {
            match request {
                rcs::FdbRemoteControlRequest::RunComponent { component_url, args, responder } => {
                    let response = self.spawn_component_and_wait(&component_url, args).await?;
                    responder.send(response).context("sending RunComponent response")?;
                }
                rcs::FdbRemoteControlRequest::StartComponent {
                    component_url,
                    args,
                    controller,
                    responder,
                } => {
                    self.spawn_component_async(&component_url, args, controller)?;
                    responder.send(true).context("sending StartComponent response")?;
                }
                rcs::FdbRemoteControlRequest::RebootDevice { reboot_type, responder } => {
                    self.reboot_device(reboot_type, responder).await?;
                }
            }
        }
        Ok(())
    }

    fn construct_admin_proxy() -> Result<fdevmgr::AdministratorProxy, Error> {
        let proxy = fuchsia_component::client::connect_to_service::<fdevmgr::AdministratorMarker>()
            .map_err(|s| format_err!("Failed to connect to DevMgr service: {}", s))?;
        return Ok(proxy);
    }

    pub fn spawn_component_async(
        &self,
        component_name: &str,
        argv: Vec<String>,
        server_end: fidl::endpoints::ServerEnd<
            fidl_fuchsia_developer_remotecontrol::ComponentControllerMarker,
        >,
    ) -> Result<(), Error> {
        log::info!("Attempting to start component '{}' with argv {:?}...", component_name, argv);
        let launcher = launcher().expect("Failed to open launcher service");
        let app = AppBuilder::new(component_name).args(argv).spawn(&launcher)?;

        let (stream, control_handle) = server_end.into_stream_and_control_handle()?;
        let controller = ComponentController::new(app, stream, control_handle);

        hoist::spawn(async move {
            controller.serve().await.unwrap();
        });

        return Ok(());
    }

    pub async fn spawn_component_and_wait(
        &self,
        component_name: &str,
        argv: Vec<String>,
    ) -> Result<rcs::RunComponentResponse, Error> {
        log::info!("Attempting to run component '{}' with argv {:?}...", component_name, argv);
        let launcher = launcher().expect("Failed to open launcher service");
        let output = AppBuilder::new(component_name).args(argv).output(&launcher)?.await?;

        let response = rcs::RunComponentResponse {
            component_stdout: Some(String::from_utf8_lossy(&output.stdout).to_string()),
            component_stderr: Some(String::from_utf8_lossy(&output.stderr).to_string()),
            exit_code: Some(output.exit_status.code().try_into().unwrap()),
        };
        Ok(response)
    }

    pub async fn reboot_device<'a>(
        &'a self,
        reboot_type: rcs::RebootType,
        responder: rcs::FdbRemoteControlRebootDeviceResponder,
    ) -> Result<(), Error> {
        responder.send().context("failed to send successful reboot response.")?;

        log::info!("got remote control request to reboot: {:?}", reboot_type);
        let suspend_flag = match reboot_type {
            rcs::RebootType::Reboot => fdevmgr::SUSPEND_FLAG_REBOOT,
            rcs::RebootType::Recovery => fdevmgr::SUSPEND_FLAG_REBOOT_RECOVERY,
            rcs::RebootType::Bootloader => fdevmgr::SUSPEND_FLAG_REBOOT_BOOTLOADER,
        };
        self.admin_proxy.suspend(suspend_flag).await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy, fidl_fuchsia_developer_remotecontrol as rcs,
        fuchsia_async as fasync, fuchsia_zircon as zx,
    };

    // This is the exit code zircon will return when a component is killed.
    const EXIT_CODE_KILLED: i64 = -1024;

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
                    Some(fdevmgr::AdministratorRequest::Suspend {
                        flags: fdevmgr::SUSPEND_FLAG_REBOOT_RECOVERY,
                        responder,
                    }) => {
                        let _ = responder.send(zx::Status::OK.into_raw());
                    }
                    Some(fdevmgr::AdministratorRequest::Suspend {
                        flags: fdevmgr::SUSPEND_FLAG_REBOOT_BOOTLOADER,
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

    async fn run_reboot_test(reboot_type: rcs::RebootType) {
        let (rcs_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<rcs::FdbRemoteControlMarker>().unwrap();
        fasync::spawn(async move {
            RemoteControlService::new_with_proxies(setup_fake_admin_service())
                .serve_stream(stream)
                .await
                .unwrap();
        });

        let response = rcs_proxy.reboot_device(reboot_type).await.unwrap();
        assert_eq!(response, ());
    }

    async fn verify_exit_code(proxy: rcs::ComponentControllerProxy, expected_exit_code: i64) {
        let events: Vec<_> = proxy.take_event_stream().collect::<Vec<_>>().await;

        assert_eq!(events.len(), 1);

        let event = events[0].as_ref().unwrap();
        match event {
            rcs::ComponentControllerEvent::OnTerminated { exit_code } => {
                assert_eq!(*exit_code, expected_exit_code);
            }
        };
    }

    fn setup_rcs() -> rcs::FdbRemoteControlProxy {
        let service = RemoteControlService::new().unwrap();

        let (rcs_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<rcs::FdbRemoteControlMarker>().unwrap();
        fasync::spawn(async move {
            service.serve_stream(stream).await.unwrap();
        });

        return rcs_proxy;
    }

    async fn run_component_test(component_url: &str, argv: Vec<&str>) -> rcs::RunComponentResponse {
        let (rcs_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<rcs::FdbRemoteControlMarker>().unwrap();
        fasync::spawn(async move {
            RemoteControlService::new().unwrap().serve_stream(stream).await.unwrap();
        });

        return rcs_proxy.run_component(component_url, &mut argv.iter().copied()).await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_spawn_hello_world() -> Result<(), Error> {
        let response = run_component_test(
            "fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx",
            vec![],
        )
        .await;

        assert_eq!(response.component_stdout, Some(String::from("Hello, world!\n")));
        assert_eq!(response.component_stderr, Some(String::default()));
        assert_eq!(response.exit_code, Some(0));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_spawn_non_existent_package() -> Result<(), Error> {
        let response = run_component_test(
            "fuchsia-pkg://fuchsia.com/hello_world_rust#meta/this_package_doesnt_exist.cmx",
            vec![],
        )
        .await;

        assert_eq!(response.component_stdout, Some(String::default()));
        assert_eq!(response.component_stderr, Some(String::default()));
        assert_eq!(response.exit_code, Some(-1));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_spawn_hello_world_async() -> Result<(), Error> {
        let rcs_proxy = setup_rcs();
        let (proxy, server_end) = create_proxy::<rcs::ComponentControllerMarker>()?;

        let argv = vec![];
        let start_response = rcs_proxy
            .start_component(
                "fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx",
                &mut argv.iter().copied(),
                server_end,
            )
            .await
            .unwrap();
        assert!(start_response);

        verify_exit_code(proxy, 0).await;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_spawn_and_kill() -> Result<(), Error> {
        let rcs_proxy = setup_rcs();
        let (proxy, server_end) = create_proxy::<rcs::ComponentControllerMarker>()?;

        let argv = vec![];
        let start_response = rcs_proxy
            .start_component(
                "fuchsia-pkg://fuchsia.com/echo_server#meta/echo_server.cmx",
                &mut argv.iter().copied(),
                server_end,
            )
            .await
            .unwrap();
        assert!(start_response);

        let kill_response = proxy.kill().await.unwrap();
        assert!(kill_response);

        verify_exit_code(proxy, EXIT_CODE_KILLED).await;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reboot() -> Result<(), Error> {
        run_reboot_test(rcs::RebootType::Reboot).await;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reboot_recovery() -> Result<(), Error> {
        run_reboot_test(rcs::RebootType::Recovery).await;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reboot_bootloader() -> Result<(), Error> {
        run_reboot_test(rcs::RebootType::Bootloader).await;
        Ok(())
    }
}
