// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use async_trait::async_trait;
use failure::{format_err, Error};
use fidl_fuchsia_device_manager as fdevmgr;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_info;
use fuchsia_zircon as zx;
use std::rc::Rc;

/// Node: SystemPowerStateHandler
///
/// Summary: Interacts with the device manager service to command system power state
///          changes (i.e., reboot, shutdown, suspend-to-ram)
///
/// Handles Messages:
///     - SystemShutdown
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fidl_fuchsia_device_manager: used by this node to command system power state changes to
///                                    the service "fuchsia.device.manager.Administrator"

/// The device manager service that we'll be communicating with
const DEV_MGR_SVC: &'static str = "/svc/fuchsia.device.manager.Administrator";

pub struct SystemPowerStateHandler {
    svc_proxy: fdevmgr::AdministratorProxy,
}

impl SystemPowerStateHandler {
    pub fn new() -> Result<Rc<Self>, Error> {
        Ok(Self::new_with_proxy(Self::connect_devmgr_service()?))
    }

    fn new_with_proxy(svc_proxy: fdevmgr::AdministratorProxy) -> Rc<Self> {
        Rc::new(Self { svc_proxy })
    }

    fn connect_devmgr_service() -> Result<fdevmgr::AdministratorProxy, Error> {
        let (client, server) =
            zx::Channel::create().map_err(|s| format_err!("Failed to create channel: {}", s))?;

        fdio::service_connect(DEV_MGR_SVC, server)
            .map_err(|s| format_err!("Failed to connect to DevMgr service: {}", s))?;
        Ok(fdevmgr::AdministratorProxy::new(fasync::Channel::from_channel(client)?))
    }

    async fn handle_system_shutdown(&self, reason: String) -> Result<MessageReturn, Error> {
        fx_log_info!("System shutdown (reason: {})", reason);
        self.dev_mgr_suspend(fdevmgr::SUSPEND_FLAG_POWEROFF).await?;
        Ok(MessageReturn::SystemShutdown)
    }

    async fn dev_mgr_suspend(&self, suspend_flag: u32) -> Result<(), Error> {
        let status = self.svc_proxy.suspend(suspend_flag).await.map_err(|e| {
            format_err!("DeviceManager Suspend failed: flag: {}; error: {}", suspend_flag, e)
        })?;
        zx::Status::ok(status).map_err(|e| {
            format_err!(
                "DeviceManager Suspend returned error: flag: {}, error: {}",
                suspend_flag,
                e
            )
        })?;
        Ok(())
    }
}

#[async_trait(?Send)]
impl Node for SystemPowerStateHandler {
    fn name(&self) -> &'static str {
        "SystemPowerStateHandler"
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, Error> {
        match msg {
            Message::SystemShutdown(reason) => {
                self.handle_system_shutdown(reason.to_string()).await
            }
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use futures::TryStreamExt;
    use std::cell::Cell;

    fn setup_fake_service(
        mut shutdown_function: impl FnMut() + 'static,
    ) -> fdevmgr::AdministratorProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fdevmgr::AdministratorMarker>().unwrap();

        fasync::spawn_local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fdevmgr::AdministratorRequest::Suspend {
                        flags: fdevmgr::SUSPEND_FLAG_POWEROFF,
                        responder,
                    }) => {
                        shutdown_function();
                        let _ = responder.send(zx::Status::OK.into_raw());
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    /// Creates a test SystemPowerStateHandler, for which the supplied `shutdown_function` will
    /// be called by the underlying FIDL endpoint when SystemShutdown is requested.
    pub fn setup_test_node(
        shutdown_function: impl FnMut() + 'static,
    ) -> Rc<SystemPowerStateHandler> {
        SystemPowerStateHandler::new_with_proxy(setup_fake_service(shutdown_function))
    }

    /// Tests that the node can handle the 'SystemShutdown' message as expected. The test node uses
    /// a fake device manager service here, so a system shutdown will not actually happen.
    #[fasync::run_singlethreaded(test)]
    async fn test_system_shutdown() {
        let shutdown_applied = Rc::new(Cell::new(false));
        let shutdown_applied_2 = shutdown_applied.clone();
        let node = setup_test_node(move || {
            shutdown_applied_2.set(true);
        });
        match node.handle_message(&Message::SystemShutdown(String::new())).await.unwrap() {
            MessageReturn::SystemShutdown => {}
            _ => assert!(false),
        }
        assert!(shutdown_applied.get());
    }
}
