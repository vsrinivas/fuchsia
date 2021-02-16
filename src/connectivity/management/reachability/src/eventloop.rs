// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The special-purpose event loop used by the reachability monitor.
//!
//! This event loop receives events from netstack. Thsose events are used by the reachability
//! monitor to infer the connectivity state.

use {
    anyhow::{anyhow, Context as _},
    fidl_fuchsia_net_interfaces as fnet_interfaces,
    fidl_fuchsia_net_interfaces_ext::{self as fnet_interfaces_ext, Update as _},
    fuchsia_async as fasync,
    fuchsia_inspect::health::Reporter,
    fuchsia_zircon as zx,
    futures::{pin_mut, prelude::*, select},
    reachability_core::Monitor,
    std::collections::HashMap,
};

const REPORT_PERIOD: zx::Duration = zx::Duration::from_seconds(60);
const PROBE_PERIOD: zx::Duration = zx::Duration::from_seconds(60);

/// The event loop.
pub struct EventLoop {
    monitor: Monitor,
    interface_properties: HashMap<u64, fnet_interfaces_ext::Properties>,
}

impl EventLoop {
    /// `new` returns an `EventLoop` instance.
    pub fn new(monitor: Monitor) -> Self {
        fuchsia_inspect::component::health().set_starting_up();
        EventLoop { monitor, interface_properties: HashMap::new() }
    }

    /// `run` starts the event loop.
    pub async fn run(&mut self) -> Result<(), anyhow::Error> {
        debug!("starting event loop");
        let if_watcher_stream = fidl_fuchsia_net_interfaces_ext::event_stream(
            self.monitor.create_interface_watcher().context("failed to get interface watcher")?,
        )
        .fuse();
        let mut probe_futures = futures::stream::FuturesUnordered::new();
        let report_stream = fasync::Interval::new(REPORT_PERIOD).fuse();
        pin_mut!(if_watcher_stream, report_stream);

        fuchsia_inspect::component::health().set_ok();

        loop {
            select! {
                if_watcher_res = if_watcher_stream.try_next() => {
                    match if_watcher_res {
                        Ok(Some(event)) => {
                            let discovered_id = self
                                .handle_interface_watcher_event(event)
                                .await
                                .context("failed to handle interface watcher event")?;
                            if let Some(id) = discovered_id {
                                probe_futures
                                    .push(fasync::Interval::new(PROBE_PERIOD)
                                    .map(move |()| id)
                                    .into_future());
                            }
                        }
                        Ok(None) => {
                            return Err(anyhow!("interface watcher stream unexpectedly ended"));
                        }
                        Err(e) => return Err(anyhow!("interface watcher stream error: {}", e)),
                    }
                }
                report = report_stream.next() => {
                    let () = report.context("periodic timer for reporting unexpectedly ended")?;
                    let () = self.monitor.report_state();
                }
                probe = probe_futures.next() => {
                    match probe {
                        Some((Some(id), stream)) => {
                            if let Some(properties) = self.interface_properties.get(&id) {
                                let () = self.monitor.compute_state(properties).await;
                                let () = probe_futures.push(stream.into_future());
                            }
                        }
                        Some((None, _)) => {
                            return Err(anyhow!(
                                "periodic timer for probing reachability unexpectedly ended"
                            ));
                        }
                        // None is returned when there are no periodic timers in the
                        // FuturesUnordered collection.
                        None => {}
                    }
                }
            }
        }
    }

    async fn handle_interface_watcher_event(
        &mut self,
        event: fnet_interfaces::Event,
    ) -> Result<Option<u64>, anyhow::Error> {
        match self
            .interface_properties
            .update(event)
            .context("failed to update interface properties map with watcher event")?
        {
            fnet_interfaces_ext::UpdateResult::Added(properties)
            | fnet_interfaces_ext::UpdateResult::Existing(properties) => {
                let id = properties.id;
                debug!("setting timer for interface {}", id);
                let () = self.monitor.compute_state(properties).await;
                return Ok(Some(id));
            }
            fnet_interfaces_ext::UpdateResult::Changed { previous: _, current: properties } => {
                let () = self.monitor.compute_state(properties).await;
            }
            fnet_interfaces_ext::UpdateResult::Removed(properties) => {
                let () = self.monitor.handle_interface_removed(properties);
            }
            fnet_interfaces_ext::UpdateResult::NoChange => {}
        }
        Ok(None)
    }
}
