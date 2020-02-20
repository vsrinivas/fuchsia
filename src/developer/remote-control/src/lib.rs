// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_control::ComponentController,
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_developer_remotecontrol as rcs, fidl_fuchsia_device_manager as fdevmgr,
    fuchsia_component::client::{launcher, AppBuilder},
    futures::prelude::*,
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
        mut stream: rcs::RemoteControlRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("next RemoteControl request")? {
            match request {
                rcs::RemoteControlRequest::StartComponent {
                    component_url,
                    args,
                    component_stdout: stdout,
                    component_stderr: stderr,
                    controller,
                    responder,
                } => {
                    let mut response = self.spawn_component_async(
                        &component_url,
                        args,
                        stdout,
                        stderr,
                        controller,
                    );
                    responder.send(&mut response).context("sending StartComponent response")?;
                }
                rcs::RemoteControlRequest::RebootDevice { reboot_type, responder } => {
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
        stdout: fidl::Socket,
        stderr: fidl::Socket,
        server_end: fidl::endpoints::ServerEnd<
            fidl_fuchsia_developer_remotecontrol::ComponentControllerMarker,
        >,
    ) -> Result<(), rcs::ComponentControlError> {
        log::info!("Attempting to start component '{}' with argv {:?}...", component_name, argv);
        let launcher = launcher().expect("Failed to open launcher service");
        let app = match AppBuilder::new(component_name)
            .stdout(stdout)
            .stderr(stderr)
            .args(argv)
            .spawn(&launcher)
        {
            Ok(app) => app,
            Err(e) => {
                log::error!("{}", e);
                return Err(rcs::ComponentControlError::ComponentControlFailure);
            }
        };

        let (stream, control_handle) = match server_end.into_stream_and_control_handle() {
            Ok((stream, control_handle)) => (stream, control_handle),
            Err(e) => {
                log::error!("{}", e);
                return Err(rcs::ComponentControlError::ControllerSetupFailure);
            }
        };
        let controller = ComponentController::new(app, stream, control_handle);

        hoist::spawn(async move {
            controller.serve().await.unwrap();
        });

        return Ok(());
    }

    pub async fn reboot_device<'a>(
        &'a self,
        reboot_type: rcs::RebootType,
        responder: rcs::RemoteControlRebootDeviceResponder,
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
    // This is the exit code zircon will return for an non-existent package.
    const EXIT_CODE_START_FAILED: i64 = -1;

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
            fidl::endpoints::create_proxy_and_stream::<rcs::RemoteControlMarker>().unwrap();
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

    fn verify_socket_content(s: fidl::Socket, expected: &str) {
        let mut value = Vec::new();

        let mut remaining = s.outstanding_read_bytes().or::<usize>(Ok(0usize)).unwrap();
        while remaining > 0 {
            let mut buf = [0u8; 128];
            let n = s.read(&mut buf).or::<usize>(Ok(0usize)).unwrap();
            value.extend_from_slice(&buf[..n]);
            remaining = s.outstanding_read_bytes().or::<usize>(Ok(0)).unwrap();
        }
        assert_eq!(std::str::from_utf8(&value).unwrap(), expected);
    }

    fn setup_rcs() -> rcs::RemoteControlProxy {
        let service = RemoteControlService::new().unwrap();

        let (rcs_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<rcs::RemoteControlMarker>().unwrap();
        fasync::spawn(async move {
            service.serve_stream(stream).await.unwrap();
        });

        return rcs_proxy;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_spawn_hello_world() -> Result<(), Error> {
        let rcs_proxy = setup_rcs();
        let (proxy, server_end) = create_proxy::<rcs::ComponentControllerMarker>()?;
        let (sout, cout) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        let (serr, cerr) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;

        let _ = rcs_proxy
            .start_component(
                "fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx",
                &mut std::iter::empty::<_>(),
                sout,
                serr,
                server_end,
            )
            .await
            .unwrap()
            .unwrap();

        verify_exit_code(proxy, 0).await;
        verify_socket_content(cout, "Hello, world!\n");
        verify_socket_content(cerr, "");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_spawn_and_kill() -> Result<(), Error> {
        let rcs_proxy = setup_rcs();
        let (proxy, server_end) = create_proxy::<rcs::ComponentControllerMarker>()?;
        let (sout, cout) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        let (serr, cerr) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;

        let _ = rcs_proxy
            .start_component(
                "fuchsia-pkg://fuchsia.com/echo_server#meta/echo_server.cmx",
                &mut std::iter::empty::<_>(),
                sout,
                serr,
                server_end,
            )
            .and_then(|_| proxy.kill())
            .await?;

        verify_exit_code(proxy, EXIT_CODE_KILLED).await;
        verify_socket_content(cout, "");
        verify_socket_content(cerr, "");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_start_non_existent_package() -> Result<(), Error> {
        let rcs_proxy = setup_rcs();
        let (proxy, server_end) = create_proxy::<rcs::ComponentControllerMarker>()?;
        let (sout, cout) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        let (serr, cerr) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;

        let _start_response = rcs_proxy
            .start_component(
                "fuchsia-pkg://fuchsia.com/hello_world_rust#meta/this_package_doesnt_exist.cmx",
                &mut std::iter::empty::<_>(),
                sout,
                serr,
                server_end,
            )
            .await
            .unwrap()
            .unwrap();

        verify_exit_code(proxy, EXIT_CODE_START_FAILED).await;
        verify_socket_content(cout, "");
        verify_socket_content(cerr, "");
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
