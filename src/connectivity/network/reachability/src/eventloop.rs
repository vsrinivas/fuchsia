// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The special-purpose event loop used by the reachability monitor.
//!
//! This event loop receives events from netstack. Thsose events are used by the reachability
//! monitor to infer the connectivity state.

use {
    anyhow::{anyhow, Context as _},
    fidl_fuchsia_hardware_network as fhardware_network, fidl_fuchsia_net_debug as fnet_debug,
    fidl_fuchsia_net_interfaces as fnet_interfaces,
    fidl_fuchsia_net_interfaces_ext::{self as fnet_interfaces_ext, Update as _},
    fidl_fuchsia_net_neighbor as fnet_neighbor, fidl_fuchsia_net_stack as fnet_stack,
    fuchsia_async::{self as fasync, TimeoutExt as _},
    fuchsia_inspect::health::Reporter,
    fuchsia_zircon as zx,
    futures::{pin_mut, prelude::*, select},
    reachability_core::{watchdog, InterfaceView, Monitor, NeighborCache},
    std::collections::HashMap,
    tracing::{debug, error},
};

const REPORT_PERIOD: zx::Duration = zx::Duration::from_seconds(60);
const PROBE_PERIOD: zx::Duration = zx::Duration::from_seconds(60);
const FIDL_TIMEOUT: zx::Duration = zx::Duration::from_seconds(90);

struct SystemDispatcher;

#[async_trait::async_trait]
impl watchdog::SystemDispatcher for SystemDispatcher {
    type DeviceDiagnostics = DeviceDiagnosticsProvider;

    async fn log_debug_info(&self) -> Result<(), watchdog::Error> {
        let diagnostics =
            fuchsia_component::client::connect_to_protocol::<fnet_debug::DiagnosticsMarker>()
                .map_err(|e| {
                    error!(e = ?e, "failed to connect to protocol");
                    watchdog::Error::NotSupported
                })?;
        diagnostics
            .log_debug_info_to_syslog()
            .map_err(watchdog::Error::Fidl)
            .on_timeout(fasync::Time::after(FIDL_TIMEOUT), || Err(watchdog::Error::Timeout))
            .await
    }

    fn get_device_diagnostics(
        &self,
        interface: u64,
    ) -> Result<Self::DeviceDiagnostics, watchdog::Error> {
        let interfaces_debug =
            fuchsia_component::client::connect_to_protocol::<fnet_debug::InterfacesMarker>()
                .map_err(|e| {
                    error!(e = ?e, "failed to connect to protocol");
                    watchdog::Error::NotSupported
                })?;
        let (port, server_end) = fidl::endpoints::create_proxy()?;
        interfaces_debug.get_port(interface, server_end)?;

        let (diagnostics, server_end) = fidl::endpoints::create_proxy()?;
        port.get_diagnostics(server_end)?;

        Ok(DeviceDiagnosticsProvider { interface, diagnostics, port })
    }
}

struct DeviceDiagnosticsProvider {
    interface: u64,
    diagnostics: fhardware_network::DiagnosticsProxy,
    port: fhardware_network::PortProxy,
}

#[async_trait::async_trait]
impl watchdog::DeviceDiagnosticsProvider for DeviceDiagnosticsProvider {
    async fn get_counters(&self) -> Result<watchdog::DeviceCounters, watchdog::Error> {
        self.port
            .get_counters()
            .map_err(watchdog::Error::Fidl)
            .and_then(
                |fhardware_network::PortGetCountersResponse { rx_frames, tx_frames, .. }| match (
                    rx_frames, tx_frames,
                ) {
                    (Some(rx_frames), Some(tx_frames)) => {
                        futures::future::ok(watchdog::DeviceCounters { rx_frames, tx_frames })
                    }
                    (None, Some(_)) | (Some(_), None) | (None, None) => {
                        error!(iface = self.interface, "missing information from port counters");
                        futures::future::err(watchdog::Error::NotSupported)
                    }
                },
            )
            .on_timeout(fasync::Time::after(FIDL_TIMEOUT), || Err(watchdog::Error::Timeout))
            .await
    }

    async fn log_debug_info(&self) -> Result<(), watchdog::Error> {
        self.diagnostics
            .log_debug_info_to_syslog()
            .map_err(watchdog::Error::Fidl)
            .on_timeout(fasync::Time::after(FIDL_TIMEOUT), || Err(watchdog::Error::Timeout))
            .await
    }
}

type Watchdog = watchdog::Watchdog<SystemDispatcher>;

/// The event loop.
pub struct EventLoop {
    monitor: Monitor,
    watchdog: Watchdog,
    interface_properties: HashMap<u64, fnet_interfaces_ext::Properties>,
    neighbor_cache: NeighborCache,
}

impl EventLoop {
    /// `new` returns an `EventLoop` instance.
    pub fn new(monitor: Monitor) -> Self {
        fuchsia_inspect::component::health().set_starting_up();
        EventLoop {
            monitor,
            watchdog: Watchdog::new(),
            interface_properties: Default::default(),
            neighbor_cache: Default::default(),
        }
    }

    /// `run` starts the event loop.
    pub async fn run(&mut self) -> Result<(), anyhow::Error> {
        use fuchsia_component::client::connect_to_protocol;

        let stack = connect_to_protocol::<fnet_stack::StackMarker>()
            .context("network_manager failed to connect to netstack")?;

        let if_watcher_stream = {
            let interface_state = connect_to_protocol::<fnet_interfaces::StateMarker>()
                .context("network_manager failed to connect to interface state")?;
            // TODO(https://fxbug.dev/110445): Either not register interest in
            // valid-until or don't calculate reachability upon valid lifetime
            // changes. Note that the event stream returned by the extension
            // crate is created from a watcher with interest in all fields.
            fnet_interfaces_ext::event_stream_from_state(&interface_state)
                .context("get interface event stream")?
                .fuse()
        };

        let neigh_watcher_stream = {
            let view = connect_to_protocol::<fnet_neighbor::ViewMarker>()
                .context("failed to connect to neighbor view")?;
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fnet_neighbor::EntryIteratorMarker>()
                    .context("failed to create EntryIterator proxy")?;
            let () = view
                .open_entry_iterator(server_end, fnet_neighbor::EntryIteratorOptions::EMPTY)
                .context("failed to open EntryIterator")?;
            futures::stream::try_unfold(proxy, |proxy| {
                proxy.get_next().map_ok(|e| {
                    Some((
                        futures::stream::iter(e.into_iter().map(Result::<_, fidl::Error>::Ok)),
                        proxy,
                    ))
                })
            })
            .try_flatten()
            .fuse()
        };

        debug!("starting event loop");
        let mut probe_futures = futures::stream::FuturesUnordered::new();
        let report_stream = fasync::Interval::new(REPORT_PERIOD).fuse();
        pin_mut!(if_watcher_stream, report_stream, neigh_watcher_stream);

        fuchsia_inspect::component::health().set_ok();

        loop {
            select! {
                if_watcher_res = if_watcher_stream.try_next() => {
                    match if_watcher_res {
                        Ok(Some(event)) => {
                            let discovered_id = self
                                .handle_interface_watcher_event(&stack, event)
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
                neigh_res = neigh_watcher_stream.try_next() => {
                    let event = neigh_res.context("neighbor stream error")?
                                         .context("neighbor stream ended")?;
                    self.neighbor_cache.process_neighbor_event(event);
                }
                report = report_stream.next() => {
                    let () = report.context("periodic timer for reporting unexpectedly ended")?;
                    let () = self.monitor.report_state();
                }
                probe = probe_futures.next() => {
                    match probe {
                        Some((Some(id), stream)) => {
                            if let Some(properties) = self.interface_properties.get(&id) {
                                Self::compute_state(
                                    &mut self.monitor,
                                    &mut self.watchdog,
                                    &stack, properties,
                                    &self.neighbor_cache
                                ).await;
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
        stack: &fnet_stack::StackProxy,
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
                Self::compute_state(
                    &mut self.monitor,
                    &mut self.watchdog,
                    &stack,
                    properties,
                    &self.neighbor_cache,
                )
                .await;
                return Ok(Some(id));
            }
            fnet_interfaces_ext::UpdateResult::Changed { previous: _, current: properties } => {
                Self::compute_state(
                    &mut self.monitor,
                    &mut self.watchdog,
                    &stack,
                    properties,
                    &self.neighbor_cache,
                )
                .await;
            }
            fnet_interfaces_ext::UpdateResult::Removed(properties) => {
                self.watchdog.handle_interface_removed(properties.id);
                self.monitor.handle_interface_removed(properties);
            }
            fnet_interfaces_ext::UpdateResult::NoChange => {}
        }
        Ok(None)
    }

    async fn compute_state(
        monitor: &mut Monitor,
        watchdog: &mut Watchdog,
        stack: &fnet_stack::StackProxy,
        properties: &fnet_interfaces_ext::Properties,
        neighbor_cache: &NeighborCache,
    ) {
        let routes = stack.get_forwarding_table().await.unwrap_or_else(|e| {
            error!("failed to get route table: {}", e);
            Vec::new()
        });

        let view = InterfaceView {
            properties,
            routes: &routes[..],
            neighbors: neighbor_cache.get_interface_neighbors(properties.id),
        };

        let monitor_fut = monitor.compute_state(view);

        let watchdog_fut =
            watchdog.check_interface_state(zx::Time::get_monotonic(), &SystemDispatcher {}, view);

        let ((), ()) = futures::future::join(monitor_fut, watchdog_fut).await;
    }
}
