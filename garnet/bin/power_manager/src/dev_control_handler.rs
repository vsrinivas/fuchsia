// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_device as fdev;
use fuchsia_zircon as zx;
use std::rc::Rc;

/// Node: DeviceControlHandler
///
/// Summary: Provides an interface to control the power, performance, and sleep states of a devhost
///          device
///
/// Handles Messages:
///     - GetPerformanceState
///     - SetPerformanceState
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fuchsia.device.Controller: the node uses this protocol to control the power, performance,
///       and sleep states of a devhost device

pub const MAX_PERF_STATES: u32 = fdev::MAX_DEVICE_PERFORMANCE_STATES;

pub struct DeviceControlHandler {
    driver_path: String,
    driver_proxy: fdev::ControllerProxy,
}

impl DeviceControlHandler {
    pub fn new(driver_path: String) -> Result<Rc<Self>, Error> {
        let proxy = Self::connect_driver(&driver_path)?;
        Ok(Self::new_with_proxy(driver_path, proxy))
    }

    fn new_with_proxy(driver_path: String, driver_proxy: fdev::ControllerProxy) -> Rc<Self> {
        Rc::new(Self { driver_path, driver_proxy })
    }

    fn connect_driver(path: &String) -> Result<fdev::ControllerProxy, Error> {
        let (proxy, server) = fidl::endpoints::create_proxy::<fdev::ControllerMarker>()
            .map_err(|e| format_err!("Failed to create Controller proxy: {}", e))?;

        fdio::service_connect(path, server.into_channel())
            .map_err(|s| format_err!("Failed to connect to service at {}: {}", path, s))?;
        Ok(proxy)
    }

    async fn handle_get_performance_state(&self) -> Result<MessageReturn, Error> {
        // TODO(fxb/43744): The controller API doesn't exist yet (fxb/43743)
        Ok(MessageReturn::GetPerformanceState(0))
    }

    async fn handle_set_performance_state(&self, in_state: u32) -> Result<MessageReturn, Error> {
        // Make the FIDL call
        let (status, out_state) =
            self.driver_proxy.set_performance_state(in_state).await.map_err(|e| {
                format_err!(
                    "{} ({}): set_performance_state IPC failed: {}",
                    self.name(),
                    self.driver_path,
                    e
                )
            })?;

        // Check the status code
        zx::Status::ok(status).map_err(|e| {
            format_err!(
                "{} ({}): set_performance_state driver returned error: {}",
                self.name(),
                self.driver_path,
                e
            )
        })?;

        // On success, in_state will equal out_state
        if in_state == out_state {
            Ok(MessageReturn::SetPerformanceState)
        } else {
            Err(format_err!(
                "{} ({}): expected in_state == out_state (in_state={}; out_state={})",
                self.name(),
                self.driver_path,
                in_state,
                out_state
            ))
        }
    }
}

#[async_trait(?Send)]
impl Node for DeviceControlHandler {
    fn name(&self) -> &'static str {
        "DeviceControlHandler"
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, Error> {
        match msg {
            Message::GetPerformanceState => self.handle_get_performance_state().await,
            Message::SetPerformanceState(state) => self.handle_set_performance_state(*state).await,
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use futures::TryStreamExt;
    use std::cell::Cell;

    fn setup_fake_driver(
        mut set_performance_state: impl FnMut(u32) + 'static,
    ) -> fdev::ControllerProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fdev::ControllerMarker>().unwrap();
        fasync::spawn_local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fdev::ControllerRequest::SetPerformanceState {
                        requested_state,
                        responder,
                    }) => {
                        set_performance_state(requested_state as u32);
                        let _ = responder.send(zx::Status::OK.into_raw(), requested_state);
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    pub fn setup_test_node(
        set_performance_state: impl FnMut(u32) + 'static,
    ) -> Rc<DeviceControlHandler> {
        DeviceControlHandler::new_with_proxy(
            "Fake".to_string(),
            setup_fake_driver(set_performance_state),
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_performance_state() {
        let recvd_perf_state = Rc::new(Cell::new(0));
        let recvd_perf_state_clone = recvd_perf_state.clone();
        let set_performance_state = move |state| {
            recvd_perf_state_clone.set(state);
        };
        let node = setup_test_node(set_performance_state);

        let new_perf_state = 1;
        match node.handle_message(&Message::SetPerformanceState(new_perf_state)).await.unwrap() {
            MessageReturn::SetPerformanceState => {}
            _ => panic!(),
        }
        assert_eq!(recvd_perf_state.get(), new_perf_state);

        let new_perf_state = 2;
        match node.handle_message(&Message::SetPerformanceState(new_perf_state)).await.unwrap() {
            MessageReturn::SetPerformanceState => {}
            _ => panic!(),
        }
        assert_eq!(recvd_perf_state.get(), new_perf_state);
    }
}
