// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[allow(unused_imports)]
use super::prelude::*;

use fuchsia_zircon::Duration;
use openthread::ot;

mod api;
mod border_agent;
mod connectivity_state;
mod convert;
mod driver_state;
mod error_adapter;
mod host_to_thread;
mod joiner;
mod srp_proxy;
mod tasks;
mod thread_to_host;

#[cfg(test)]
mod tests;

pub use border_agent::*;
pub use connectivity_state::*;
pub use convert::*;
use driver_state::*;
pub use error_adapter::*;
pub use host_to_thread::*;
use lowpan_driver_common::net::{BackboneInterface, NetworkInterface};
use lowpan_driver_common::AsyncCondition;
pub use srp_proxy::*;
pub use thread_to_host::*;

const DEFAULT_SCAN_DWELL_TIME_MS: u32 = 200;

#[cfg(not(test))]
const DEFAULT_TIMEOUT: Duration = Duration::from_seconds(5);

#[cfg(test)]
const DEFAULT_TIMEOUT: Duration = Duration::from_seconds(90);

#[allow(unused)]
const JOIN_TIMEOUT: Duration = Duration::from_seconds(120);

/// Extra time that is added to the network/energy scans, in addition to the calculated timeout.
const SCAN_EXTRA_TIMEOUT: Duration = Duration::from_seconds(10);

#[allow(unused)]
const STD_IPV6_NET_PREFIX_LEN: u8 = 64;

#[derive(thiserror::Error, Debug, Eq, PartialEq)]
pub struct ResetRequested;

impl std::fmt::Display for ResetRequested {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

#[derive(Debug)]
pub struct OtDriver<OT, NI, BI> {
    /// Internal, mutex-protected driver state.
    driver_state: parking_lot::Mutex<DriverState<OT>>,

    /// Condition that fires whenever the above `driver_state` changes.
    driver_state_change: AsyncCondition,

    /// Network Interface. Provides the interface to netstack.
    net_if: NI,

    /// Backbone Interface. Provides support of bouder routing and TREL.
    backbone_if: BI,

    /// Output receiver for OpenThread CLI
    cli_output_receiver: futures::lock::Mutex<futures::channel::mpsc::UnboundedReceiver<String>>,

    /// Task for updating mDNS service for border agent
    border_agent_service: parking_lot::Mutex<Option<fasync::Task<Result<(), anyhow::Error>>>>,

    /// Additional TXT records set by `meshcop_update_txt_entries`.
    border_agent_txt_entries: futures::lock::Mutex<Vec<(String, Vec<u8>)>>,
}

impl<OT: ot::Cli, NI, BI> OtDriver<OT, NI, BI> {
    pub fn new(ot_instance: OT, net_if: NI, backbone_if: BI) -> Self {
        let (cli_output_sender, cli_output_receiver) = futures::channel::mpsc::unbounded();

        ot_instance.cli_init(move |c_str| {
            cli_output_sender.unbounded_send(c_str.to_string_lossy().into_owned()).unwrap();
        });

        OtDriver {
            driver_state: parking_lot::Mutex::new(DriverState::new(ot_instance)),
            driver_state_change: AsyncCondition::new(),
            net_if,
            backbone_if,
            cli_output_receiver: futures::lock::Mutex::new(cli_output_receiver),
            border_agent_service: parking_lot::Mutex::new(None),
            border_agent_txt_entries: futures::lock::Mutex::new(vec![]),
        }
    }

    /// Decorates the given future with error mapping,
    /// reset handling, and a standard timeout.
    pub fn apply_standard_combinators<'a, F>(
        &'a self,
        future: F,
    ) -> impl Future<Output = ZxResult<F::Ok>> + 'a
    where
        F: TryFuture<Error = anyhow::Error> + Unpin + Send + 'a,
        <F as TryFuture>::Ok: Send,
    {
        future
            .inspect_err(|e| fx_log_err!("apply_standard_combinators: error is \"{:?}\"", e))
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))
            .on_timeout(fasync::Time::after(DEFAULT_TIMEOUT), || {
                fx_log_err!("Timeout");
                Err(ZxStatus::TIMED_OUT)
            })
    }

    pub fn is_net_type_supported(&self, net_type: &str) -> bool {
        net_type.starts_with(fidl_fuchsia_lowpan_device::NET_TYPE_THREAD_1_X)
    }
}

/// Helper type for performing cleanup operations when dropped.
struct CleanupFunc<F: FnOnce()>(Option<F>);
impl<F: FnOnce()> CleanupFunc<F> {
    /// Disarms the cleanup func so that it will not execute when dropped.
    #[allow(dead_code)]
    fn disarm(&mut self) {
        let _ = self.0.take();
    }
}
impl<F: FnOnce()> Drop for CleanupFunc<F> {
    fn drop(&mut self) {
        if let Some(func) = self.0.take() {
            func();
        }
    }
}
