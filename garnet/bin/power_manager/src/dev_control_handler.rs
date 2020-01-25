// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::utils::connect_proxy;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_device as fdev;
use fuchsia_syslog::fx_log_err;
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

/// A builder for constructing the DeviceControlhandler node
pub struct DeviceControlHandlerBuilder {
    driver_path: String,
    driver_proxy: Option<fdev::ControllerProxy>,
}

impl DeviceControlHandlerBuilder {
    pub fn new_with_driver_path(driver_path: String) -> Self {
        Self { driver_path, driver_proxy: None }
    }

    #[cfg(test)]
    pub fn new_with_proxy(driver_path: String, proxy: fdev::ControllerProxy) -> Self {
        Self { driver_path, driver_proxy: Some(proxy) }
    }

    pub fn build(self) -> Result<Rc<DeviceControlHandler>, Error> {
        // Get default proxy if necessary
        let proxy = if self.driver_proxy.is_none() {
            connect_proxy::<fdev::ControllerMarker>(&self.driver_path)?
        } else {
            self.driver_proxy.unwrap()
        };

        Ok(Rc::new(DeviceControlHandler { driver_path: self.driver_path, driver_proxy: proxy }))
    }
}

pub struct DeviceControlHandler {
    driver_path: String,
    driver_proxy: fdev::ControllerProxy,
}

impl DeviceControlHandler {
    async fn handle_get_performance_state(&self) -> Result<MessageReturn, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "DeviceControlHandler::handle_get_performance_state",
            "driver" => self.driver_path.as_str()
        );
        // TODO(fxb/43744): The controller API doesn't exist yet (fxb/43743)
        Ok(MessageReturn::GetPerformanceState(0))
    }

    async fn handle_set_performance_state(&self, in_state: u32) -> Result<MessageReturn, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "DeviceControlHandler::handle_set_performance_state",
            "driver" => self.driver_path.as_str(),
            "state" => in_state
        );

        // Make the FIDL call
        let result = self.driver_proxy.set_performance_state(in_state).await.map_err(|e| {
            format_err!(
                "{} ({}): set_performance_state IPC failed: {}",
                self.name(),
                self.driver_path,
                e
            )
        });

        log_if_err!(result, "Failed to set performance state");
        fuchsia_trace::instant!(
            "power_manager",
            "DeviceControlHandler::set_performance_state_result",
            fuchsia_trace::Scope::Thread,
            "driver" => self.driver_path.as_str(),
            "result" => format!("{:?}", result).as_str()
        );
        let (status, out_state) = result?;

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
        DeviceControlHandlerBuilder::new_with_proxy(
            "Fake".to_string(),
            setup_fake_driver(set_performance_state),
        )
        .build()
        .unwrap()
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
