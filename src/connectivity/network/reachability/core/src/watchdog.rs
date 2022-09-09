// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides an interface health watchdog.
//!
//! The watchdog uses gateway neighbor reachability information and interface
//! counters to evaluate interface health and triggers debug information dumps
//! on the system logs when it finds unhealthy interfaces.

use crate::{neighbor_cache::NeighborHealth, Id as InterfaceId, InterfaceView};
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fuchsia_zircon as zx;
use itertools::Itertools as _;
use std::collections::HashMap;
use tracing::{error, info, warn};

/// The minimum amount of time for a device counter to be stuck in the same
/// value for the device to be considered unhealthy.
const DEVICE_COUNTERS_UNHEALTHY_TIME: zx::Duration = zx::Duration::from_minutes(2);

/// The minimum amount of time to wait before generating a new request for debug
/// information.
const DEBUG_INFO_COOLDOWN: zx::Duration = zx::Duration::from_minutes(15);

/// The minimum amount of time for a neighbor in stale state to be considered
/// unhealthy and trigger any actions.
const NEIGHBOR_STALE_AS_UNHEALTHY_TIME: zx::Duration = zx::Duration::from_minutes(2);

/// The minimum amount of time for a neighbor in unhealthy state to trigger
/// actions.
const NEIGHBOR_UNHEALTHY_TIME: zx::Duration = zx::Duration::from_minutes(1);

#[derive(Debug, thiserror::Error)]
#[cfg_attr(test, derive(Clone))]
pub enum Error {
    #[error("Operation timed out")]
    Timeout,
    #[error("FIDL error {0}")]
    Fidl(fidl::Error),
}

#[derive(Debug)]
#[cfg_attr(test, derive(Copy, Clone))]
pub struct DeviceCounters {
    pub rx_frames: u64,
    pub tx_frames: u64,
}

#[derive(Debug)]
#[cfg_attr(test, derive(Eq, PartialEq))]
struct TimestampedCounter {
    value: u64,
    at: zx::Time,
}

impl TimestampedCounter {
    fn update(&mut self, new_value: u64, new_at: zx::Time) -> bool {
        let Self { value, at } = self;
        if new_value != *value {
            *at = new_at;
            *value = new_value;
            true
        } else {
            false
        }
    }
}

#[async_trait::async_trait]
pub trait DeviceDiagnosticsProvider {
    async fn get_counters(&self) -> Result<DeviceCounters, Error>;

    async fn log_debug_info(&self) -> Result<(), Error>;
}

#[derive(Debug)]
#[cfg_attr(test, derive(Eq, PartialEq))]
enum HealthStatus {
    Unhealthy { last_action: zx::Time },
    Healthy { last_action: Option<zx::Time> },
}

impl HealthStatus {
    /// Sets the status to unhealthy at time `now`.
    ///
    /// Return `true` if a debug info action should be triggered respecting
    /// cooldown.
    fn set_unhealthy_and_check_for_debug_info_cooldown(&mut self, now: zx::Time) -> bool {
        let last_action = match self {
            HealthStatus::Unhealthy { last_action } => Some(*last_action),
            HealthStatus::Healthy { last_action } => *last_action,
        };

        let (trigger_debug_info, last_action) = match last_action
            .map(|last_action| (now - last_action >= DEBUG_INFO_COOLDOWN, last_action))
        {
            // Either we haven't yet triggered a debug info action or we've
            // passed the cooldown period.
            None | Some((true, _)) => (true, now),
            // We're still in the cooldown period from the last triggered
            // action.
            Some((false, last_action)) => (false, last_action),
        };

        *self = HealthStatus::Unhealthy { last_action: last_action };

        return trigger_debug_info;
    }
}

#[derive(Debug)]
struct InterfaceDiagnosticsState<D> {
    diagnostics: D,
    rx: TimestampedCounter,
    tx: TimestampedCounter,
    updated_at: zx::Time,
    health: HealthStatus,
}

#[derive(Debug)]
struct InterfaceState<D> {
    diagnostics_state: Option<InterfaceDiagnosticsState<D>>,
}

pub struct Watchdog<S: SystemDispatcher> {
    interfaces: HashMap<InterfaceId, InterfaceState<S::DeviceDiagnostics>>,
    system_health_status: HealthStatus,
    _marker: std::marker::PhantomData<S>,
}

#[async_trait::async_trait]
pub trait SystemDispatcher {
    type DeviceDiagnostics: DeviceDiagnosticsProvider;

    async fn log_debug_info(&self) -> Result<(), Error>;

    fn get_device_diagnostics(
        &self,
        interface: InterfaceId,
    ) -> Result<Self::DeviceDiagnostics, Error>;
}

#[derive(Debug, Eq, PartialEq, Clone, Copy)]
enum ActionReason {
    CantFetchCounters,
    DeviceRxStall,
    DeviceTxStall,
}

#[derive(Debug, Eq, PartialEq)]
struct Action {
    trigger_stack_diagnosis: bool,
    trigger_device_diagnosis: bool,
    reason: ActionReason,
}

impl<S> Watchdog<S>
where
    S: SystemDispatcher,
{
    pub fn new() -> Self {
        Self {
            interfaces: HashMap::new(),
            system_health_status: HealthStatus::Healthy { last_action: None },
            _marker: std::marker::PhantomData,
        }
    }

    async fn initialize_interface_state(
        now: zx::Time,
        sys: &S,
        interface: InterfaceId,
    ) -> Option<InterfaceDiagnosticsState<S::DeviceDiagnostics>> {
        // Get a diagnostics handle and read the initial counters.
        let diagnostics = match sys.get_device_diagnostics(interface) {
            Ok(d) => d,
            Err(e) => {
                warn!(
                    err = ?e,
                    iface = interface,
                    "failed to read diagnostics state, assuming unsupported interface"
                );
                return None;
            }
        };
        let DeviceCounters { rx_frames, tx_frames } = match diagnostics.get_counters().await {
            Ok(c) => c,
            Err(e) => {
                warn!(
                    err = ?e,
                    iface = interface,
                    "failed to read device counters, assuming unsupported interface"
                );
                return None;
            }
        };
        Some(InterfaceDiagnosticsState {
            diagnostics,
            rx: TimestampedCounter { value: rx_frames, at: now },
            tx: TimestampedCounter { value: tx_frames, at: now },
            updated_at: now,
            health: HealthStatus::Healthy { last_action: None },
        })
    }

    pub async fn check_interface_state(&mut self, now: zx::Time, sys: &S, view: InterfaceView<'_>) {
        let Self { interfaces, system_health_status, _marker: _ } = self;

        let interface = view.properties.id;

        let InterfaceState { diagnostics_state } = match interfaces.entry(interface) {
            std::collections::hash_map::Entry::Occupied(entry) => entry.into_mut(),
            std::collections::hash_map::Entry::Vacant(vacant) => vacant.insert(InterfaceState {
                diagnostics_state: Self::initialize_interface_state(now, sys, interface).await,
            }),
        };

        let diagnostics_state = if let Some(d) = diagnostics_state.as_mut() {
            d
        } else {
            // Do nothing for unsupported interfaces, we can't get counters or
            // trigger debug info on them.
            return;
        };

        if let Some(action) = Self::evaluate_interface_state(now, diagnostics_state, view).await {
            info!(
                action = ?action,
                iface = interface,
                "bad state detected, action requested"
            );
            let Action { trigger_stack_diagnosis, trigger_device_diagnosis, reason: _ } = action;
            if trigger_device_diagnosis {
                diagnostics_state.diagnostics.log_debug_info().await.unwrap_or_else(
                    |e| error!(err = ?e, iface = interface, "failed to request device debug info"),
                );
            }
            if trigger_stack_diagnosis {
                if system_health_status.set_unhealthy_and_check_for_debug_info_cooldown(now) {
                    sys.log_debug_info().await.unwrap_or_else(
                        |e| error!(err = ?e, "failed to request system debug info"),
                    );
                }
            }
        }
    }

    /// Evaluates the given interface state, returning an optional debugging
    /// action to be triggered.
    ///
    /// Interfaces are evaluated at two levels. First, all the gateways are
    /// evaluated against the neighbor table. Second, if all gateways are
    /// unhealthy, the device counters are polled until a stall is observed. If
    /// an Rx or Tx stall is seen, a debug action will be requested.
    ///
    /// If there's a timeout attempting to fetch interface counters, a debug
    /// request may also be issued.
    async fn evaluate_interface_state(
        now: zx::Time,
        diag_state: &mut InterfaceDiagnosticsState<S::DeviceDiagnostics>,
        InterfaceView {
            properties: fnet_interfaces_ext::Properties { id: interface, .. },
            routes,
            neighbors,
        }: InterfaceView<'_>,
    ) -> Option<Action> {
        let InterfaceDiagnosticsState { diagnostics, rx, tx, updated_at, health } = diag_state;
        let interface = *interface;

        let mut neighbors = neighbors.as_ref()?.iter_health();

        let found_healthy_gateway = neighbors
            .fold_while(None, |found_healthy_gateway, (neighbor, health)| {
                let is_router = routes.iter().any(|route| {
                    route.device_id == interface
                        && route
                            .next_hop
                            .as_ref()
                            .map(|next_hop| neighbor == &**next_hop)
                            .unwrap_or(false)
                });
                if !is_router {
                    return itertools::FoldWhile::Continue(found_healthy_gateway);
                }

                if is_healthy_gateway(health, now) {
                    return itertools::FoldWhile::Done(Some(true));
                }

                itertools::FoldWhile::Continue(Some(false))
            })
            .into_inner();

        match found_healthy_gateway {
            // If there are no gateways or there's at least one healthy gateway,
            // there's no action to be taken.
            None | Some(true) => {
                return None;
            }
            // If we found at least one gateway and they're all unhealthy,
            // proceed to check device counters.
            Some(false) => (),
        }

        let counters = match diagnostics.get_counters().await {
            Ok(counters) => counters,
            Err(Error::Timeout) => {
                return Some(Action {
                    trigger_stack_diagnosis: false,
                    trigger_device_diagnosis: true,
                    reason: ActionReason::CantFetchCounters,
                });
            }
            Err(Error::Fidl(e)) => {
                if !e.is_closed() {
                    error!(
                        e = ?e,
                        iface = interface,
                        "failed to read counters for interface, no action will be taken"
                    );
                }
                return None;
            }
        };
        let DeviceCounters { rx_frames, tx_frames } = counters;
        if !rx.update(rx_frames, now) {
            warn!(
                rx = ?rx,
                now = now.into_nanos(),
                iface = interface,
                "failed to observe rx traffic since last check"
            );
        }
        if !tx.update(tx_frames, now) {
            warn!(
                tx = ?tx,
                now = now.into_nanos(),
                iface = interface,
                "failed to observe tx traffic since last check"
            );
        }
        *updated_at = now;
        if let Some(reason) = [(rx, ActionReason::DeviceRxStall), (tx, ActionReason::DeviceTxStall)]
            .iter()
            .find_map(|(TimestampedCounter { value: _, at }, reason)| {
                (now - *at >= DEVICE_COUNTERS_UNHEALTHY_TIME).then(|| *reason)
            })
        {
            let action = health.set_unhealthy_and_check_for_debug_info_cooldown(now).then(|| {
                Action { trigger_stack_diagnosis: true, trigger_device_diagnosis: true, reason }
            });

            return action;
        }

        // Counters are not stalled, mark the interface as healthy.
        *health = match health {
            HealthStatus::Unhealthy { last_action } => {
                HealthStatus::Healthy { last_action: Some(*last_action) }
            }
            HealthStatus::Healthy { last_action } => {
                HealthStatus::Healthy { last_action: *last_action }
            }
        };

        None
    }
}

/// Checks if a gateway with reported `health` should be considered healthy.
fn is_healthy_gateway(health: &NeighborHealth, now: zx::Time) -> bool {
    match health {
        NeighborHealth::Unknown
        | NeighborHealth::Healthy { last_observed: _ }
        | NeighborHealth::Stale { last_observed: _, last_healthy: None }
        | NeighborHealth::Unhealthy { last_healthy: None } => true,
        NeighborHealth::Stale { last_observed, last_healthy: Some(_) } => {
            now - *last_observed < NEIGHBOR_STALE_AS_UNHEALTHY_TIME
        }
        NeighborHealth::Unhealthy { last_healthy: Some(last_healthy) } => {
            now - *last_healthy < NEIGHBOR_UNHEALTHY_TIME
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use fidl_fuchsia_net as fnet;
    use fidl_fuchsia_net_interfaces as fnet_interfaces;
    use fidl_fuchsia_net_stack as fnet_stack;
    use futures::FutureExt as _;
    use net_declare::{fidl_ip, fidl_subnet};
    use std::sync::{Arc, Mutex};

    use crate::neighbor_cache::NeighborState;

    #[test]
    fn health_status_healthy() {
        let now = SOME_TIME;
        let mut status = HealthStatus::Healthy { last_action: None };
        assert!(status.set_unhealthy_and_check_for_debug_info_cooldown(now));
        assert_eq!(status, HealthStatus::Unhealthy { last_action: now });

        status = HealthStatus::Healthy { last_action: Some(now) };
        let later = now + zx::Duration::from_seconds(1);
        assert!(!status.set_unhealthy_and_check_for_debug_info_cooldown(later));
        assert_eq!(status, HealthStatus::Unhealthy { last_action: now });

        status = HealthStatus::Healthy { last_action: Some(now) };
        let later = now + DEBUG_INFO_COOLDOWN;
        assert!(status.set_unhealthy_and_check_for_debug_info_cooldown(later));
        assert_eq!(status, HealthStatus::Unhealthy { last_action: later });
    }

    #[test]
    fn health_status_unhealthy() {
        let now = SOME_TIME;
        let mut status = HealthStatus::Unhealthy { last_action: now };
        let later = now + zx::Duration::from_seconds(1);
        assert!(!status.set_unhealthy_and_check_for_debug_info_cooldown(later));
        assert_eq!(status, HealthStatus::Unhealthy { last_action: now });

        let later = now + DEBUG_INFO_COOLDOWN;
        assert!(status.set_unhealthy_and_check_for_debug_info_cooldown(later));
        assert_eq!(status, HealthStatus::Unhealthy { last_action: later });
    }

    #[test]
    fn timestamped_counter() {
        let now = SOME_TIME;
        let mut counter = TimestampedCounter { value: 1, at: now };

        let later = now + zx::Duration::from_seconds(1);
        assert!(!counter.update(1, later));
        assert_eq!(counter, TimestampedCounter { value: 1, at: now });

        assert!(counter.update(2, later));
        assert_eq!(counter, TimestampedCounter { value: 2, at: later });
    }

    #[fuchsia::test]
    async fn initialize_interface_state() {
        let now = SOME_TIME;

        let sys = MockSystem::default();
        assert_matches!(Watchdog::initialize_interface_state(now, &sys, IFACE1).await, None);

        let counters = DeviceCounters { rx_frames: 1, tx_frames: 2 };
        sys.insert_interface_diagnostics(IFACE1);
        sys.increment_counters(IFACE1, counters.clone());

        let InterfaceDiagnosticsState { diagnostics: _, rx, tx, updated_at, health } =
            Watchdog::initialize_interface_state(now, &sys, IFACE1)
                .await
                .expect("failed to init interface");
        assert_eq!(rx, TimestampedCounter { value: counters.rx_frames, at: now });
        assert_eq!(tx, TimestampedCounter { value: counters.tx_frames, at: now });
        assert_eq!(updated_at, now);
        assert_eq!(health, HealthStatus::Healthy { last_action: None });
    }

    #[fuchsia::test]
    async fn no_action_if_no_neighbors() {
        let sys = MockSystem::default();
        let now = SOME_TIME;
        let mut state = sys.new_diagnostics_state(now, IFACE1);
        let view = MockInterfaceView::new(IFACE1, None, None);
        assert_eq!(Watchdog::evaluate_interface_state(now, &mut state, view.view()).await, None);
        assert_eq!(
            Watchdog::evaluate_interface_state(
                now,
                &mut state,
                InterfaceView { neighbors: None, ..view.view() }
            )
            .await,
            None
        );
    }

    #[fuchsia::test]
    async fn no_action_if_unreachable_neighbor_isnt_gateway() {
        let sys = MockSystem::default();
        let now = SOME_TIME;
        let mut state = sys.new_diagnostics_state(now, IFACE1);
        let view = MockInterfaceView::new(IFACE1, None, [(NEIGH1, UNHEALTHY_NEIGHBOR)]);
        assert_eq!(Watchdog::evaluate_interface_state(now, &mut state, view.view()).await, None);
    }

    #[fuchsia::test]
    async fn poll_counters_if_neighbor_is_gateway() {
        let sys = MockSystem::default();
        let now = SOME_TIME;
        let mut state = sys.new_diagnostics_state(now, IFACE1);
        let view = MockInterfaceView::new(
            IFACE1,
            [new_gateway_route(IFACE1, NEIGH1)],
            [(NEIGH1, UNHEALTHY_NEIGHBOR)],
        );
        sys.set_counters_return_timeout(IFACE1);
        assert_eq!(
            Watchdog::evaluate_interface_state(now, &mut state, view.view()).await,
            Some(Action {
                trigger_stack_diagnosis: false,
                trigger_device_diagnosis: true,
                reason: ActionReason::CantFetchCounters
            })
        );
    }

    #[fuchsia::test]
    async fn no_action_if_one_gateway_is_healthy() {
        let sys = MockSystem::default();
        let now = SOME_TIME;
        let mut state = sys.new_diagnostics_state(now, IFACE1);
        let view = MockInterfaceView::new(
            IFACE1,
            [new_gateway_route(IFACE1, NEIGH1), new_gateway_route(IFACE1, NEIGH2)],
            [(NEIGH1, UNHEALTHY_NEIGHBOR), (NEIGH2, HEALTHY_NEIGHBOR)],
        );
        assert_eq!(Watchdog::evaluate_interface_state(now, &mut state, view.view()).await, None);
    }

    #[fuchsia::test]
    async fn actions_from_counters() {
        let sys = MockSystem::default();
        let now = SOME_TIME;
        let mut state = sys.new_diagnostics_state(now, IFACE1);
        let view = MockInterfaceView::new(
            IFACE1,
            [new_gateway_route(IFACE1, NEIGH1)],
            [(NEIGH1, UNHEALTHY_NEIGHBOR)],
        );
        let now = now + DEVICE_COUNTERS_UNHEALTHY_TIME;
        sys.increment_counters(IFACE1, DeviceCounters { rx_frames: 10, tx_frames: 10 });
        assert_eq!(Watchdog::evaluate_interface_state(now, &mut state, view.view()).await, None);

        let now = now + DEVICE_COUNTERS_UNHEALTHY_TIME;
        sys.increment_counters(IFACE1, DeviceCounters { rx_frames: 0, tx_frames: 10 });
        assert_eq!(
            Watchdog::evaluate_interface_state(now, &mut state, view.view()).await,
            Some(Action {
                trigger_stack_diagnosis: true,
                trigger_device_diagnosis: true,
                reason: ActionReason::DeviceRxStall
            })
        );
        sys.increment_counters(IFACE1, DeviceCounters { rx_frames: 10, tx_frames: 0 });

        let now = now + DEBUG_INFO_COOLDOWN - zx::Duration::from_seconds(1);
        // Don't trigger again because of cooldown.
        assert_eq!(Watchdog::evaluate_interface_state(now, &mut state, view.view()).await, None);

        // Now detect a tx stall.
        sys.increment_counters(IFACE1, DeviceCounters { rx_frames: 10, tx_frames: 0 });
        let now = now + zx::Duration::from_seconds(1);
        assert_eq!(
            Watchdog::evaluate_interface_state(now, &mut state, view.view()).await,
            Some(Action {
                trigger_stack_diagnosis: true,
                trigger_device_diagnosis: true,
                reason: ActionReason::DeviceTxStall
            })
        );
    }

    #[fuchsia::test]
    async fn triggers_diagnostics_requests() {
        let sys = MockSystem::default();
        sys.insert_interface_diagnostics(IFACE1);
        let now = SOME_TIME;
        let view = MockInterfaceView::new(
            IFACE1,
            [new_gateway_route(IFACE1, NEIGH1)],
            [(NEIGH1, UNHEALTHY_NEIGHBOR)],
        );

        let mut watchdog = Watchdog::new();
        watchdog.check_interface_state(now, &sys, view.view()).await;
        assert!(!sys.take_interface_debug_requested(IFACE1));
        assert!(!sys.take_system_debug_requested());

        let now = now + DEVICE_COUNTERS_UNHEALTHY_TIME;
        watchdog.check_interface_state(now, &sys, view.view()).await;
        assert!(sys.take_interface_debug_requested(IFACE1));
        assert!(sys.take_system_debug_requested());

        // Still unhealthy, but cooling down on debug requests.
        let now = now + DEBUG_INFO_COOLDOWN / 2;
        watchdog.check_interface_state(now, &sys, view.view()).await;
        assert!(!sys.take_interface_debug_requested(IFACE1));
        assert!(!sys.take_system_debug_requested());

        let now = now + DEBUG_INFO_COOLDOWN;
        watchdog.check_interface_state(now, &sys, view.view()).await;
        assert!(sys.take_interface_debug_requested(IFACE1));
        assert!(sys.take_system_debug_requested());
    }

    #[fuchsia::test]
    fn gateway_health() {
        let now = SOME_TIME;

        // Healthy neighbor is never considered unhealthy.
        assert!(is_healthy_gateway(&NeighborHealth::Healthy { last_observed: now }, now));
        assert!(is_healthy_gateway(
            &NeighborHealth::Healthy { last_observed: now },
            now + zx::Duration::from_minutes(60)
        ));

        // Neighbor is unhealthy and has never been healthy; it is not
        // treated as unhealthy.
        assert!(is_healthy_gateway(&NeighborHealth::Unhealthy { last_healthy: None }, now));

        // Unhealthy neighbor is only considered unhealthy gateway after some
        // time.
        assert!(is_healthy_gateway(&NeighborHealth::Unhealthy { last_healthy: Some(now) }, now));
        assert!(!is_healthy_gateway(
            &NeighborHealth::Unhealthy { last_healthy: Some(now) },
            now + NEIGHBOR_UNHEALTHY_TIME
        ));

        // Stale neighbor is considered unhealthy gateway after some time *and*
        // if it's been healthy at some point.
        assert!(is_healthy_gateway(
            &NeighborHealth::Stale { last_observed: now, last_healthy: None },
            now
        ));
        assert!(is_healthy_gateway(
            &NeighborHealth::Stale { last_observed: now, last_healthy: None },
            now + NEIGHBOR_STALE_AS_UNHEALTHY_TIME
        ));
        assert!(is_healthy_gateway(
            &NeighborHealth::Stale { last_observed: now, last_healthy: Some(now) },
            now
        ));
        assert!(!is_healthy_gateway(
            &NeighborHealth::Stale { last_observed: now, last_healthy: Some(now) },
            now + NEIGHBOR_STALE_AS_UNHEALTHY_TIME
        ));
    }

    const ZERO_TIME: zx::Time = zx::Time::from_nanos(0);
    const SOME_TIME: zx::Time = zx::Time::from_nanos(NEIGHBOR_UNHEALTHY_TIME.into_nanos());
    const UNHEALTHY_NEIGHBOR: NeighborState =
        NeighborState::new(NeighborHealth::Unhealthy { last_healthy: Some(ZERO_TIME) });
    const HEALTHY_NEIGHBOR: NeighborState =
        NeighborState::new(NeighborHealth::Healthy { last_observed: ZERO_TIME });

    const IFACE1: InterfaceId = 1;
    const NEIGH1: fnet::IpAddress = fidl_ip!("192.0.2.1");
    const NEIGH2: fnet::IpAddress = fidl_ip!("2001:db8::1");

    fn new_gateway_route(
        iface: InterfaceId,
        neighbor: fnet::IpAddress,
    ) -> fnet_stack::ForwardingEntry {
        fnet_stack::ForwardingEntry {
            // NB: this field is not used; we can put whatever here.
            subnet: fidl_subnet!("0.0.0.0/0"),
            device_id: iface,
            next_hop: Some(Box::new(neighbor)),
            metric: 1,
        }
    }

    struct MockInterfaceView {
        properties: fnet_interfaces_ext::Properties,
        routes: Vec<fnet_stack::ForwardingEntry>,
        neighbors: crate::InterfaceNeighborCache,
    }

    impl MockInterfaceView {
        fn new<
            R: IntoIterator<Item = fnet_stack::ForwardingEntry>,
            N: IntoIterator<Item = (fnet::IpAddress, NeighborState)>,
        >(
            id: InterfaceId,
            routes: R,
            neighbors: N,
        ) -> Self {
            Self {
                properties: fnet_interfaces_ext::Properties {
                    id,
                    name: "foo".to_owned(),
                    device_class: fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {}),
                    online: true,
                    addresses: vec![],
                    has_default_ipv4_route: true,
                    has_default_ipv6_route: true,
                },
                routes: routes.into_iter().collect(),
                neighbors: neighbors.into_iter().collect(),
            }
        }

        fn view(&self) -> InterfaceView<'_> {
            let Self { properties, routes, neighbors } = self;
            InterfaceView { properties, routes: &routes[..], neighbors: Some(neighbors) }
        }
    }

    #[derive(Debug)]
    struct MockCounterState {
        counters_result: Option<Result<DeviceCounters, Error>>,
        debug_requested: bool,
    }

    type MockState = Arc<Mutex<HashMap<InterfaceId, MockCounterState>>>;

    type Watchdog = super::Watchdog<MockSystem>;

    #[derive(Default)]
    struct MockSystem {
        inner: MockState,
        debug_info_requested: std::sync::atomic::AtomicBool,
    }

    #[async_trait::async_trait]
    impl SystemDispatcher for MockSystem {
        type DeviceDiagnostics = MockDiagnostics;

        async fn log_debug_info(&self) -> Result<(), Error> {
            let Self { inner: _, debug_info_requested } = self;
            debug_info_requested.store(true, std::sync::atomic::Ordering::SeqCst);
            Ok(())
        }

        fn get_device_diagnostics(
            &self,
            interface: InterfaceId,
        ) -> Result<Self::DeviceDiagnostics, Error> {
            let Self { inner, debug_info_requested: _ } = self;
            Ok(MockDiagnostics { inner: inner.clone(), interface })
        }
    }

    impl MockSystem {
        fn insert_interface_diagnostics(&self, interface: InterfaceId) {
            let counters = DeviceCounters { rx_frames: 0, tx_frames: 0 };
            assert_matches!(
                self.inner.lock().unwrap().insert(
                    interface,
                    MockCounterState {
                        counters_result: Some(Ok(counters)),
                        debug_requested: false
                    }
                ),
                None
            );
        }

        fn new_diagnostics_state(
            &self,
            now: zx::Time,
            interface: InterfaceId,
        ) -> InterfaceDiagnosticsState<MockDiagnostics> {
            self.insert_interface_diagnostics(interface);
            let state = Watchdog::initialize_interface_state(now, self, interface)
                .now_or_never()
                .expect("future should be ready")
                .expect("failed to initialize interface state");

            // Remove the initial counters to force tests that use this function
            // to explicitly set any counter values they may wish to use.
            self.inner.lock().unwrap().get_mut(&interface).unwrap().counters_result = None;

            state
        }

        fn set_counters_return_timeout(&self, interface: InterfaceId) {
            self.inner.lock().unwrap().get_mut(&interface).unwrap().counters_result =
                Some(Err(Error::Timeout));
        }

        fn increment_counters(
            &self,
            interface: InterfaceId,
            DeviceCounters { rx_frames: rx, tx_frames: tx }: DeviceCounters,
        ) {
            let mut state = self.inner.lock().unwrap();
            let MockCounterState { counters_result, debug_requested: _ } =
                state.get_mut(&interface).unwrap();
            *counters_result = Some(Ok(match counters_result {
                Some(Ok(DeviceCounters { rx_frames, tx_frames })) => {
                    DeviceCounters { rx_frames: *rx_frames + rx, tx_frames: *tx_frames + tx }
                }
                None | Some(Err(_)) => DeviceCounters { rx_frames: rx, tx_frames: tx },
            }));
        }

        fn take_interface_debug_requested(&self, interface: InterfaceId) -> bool {
            let mut state = self.inner.lock().unwrap();
            if let Some(MockCounterState { counters_result: _, debug_requested }) =
                state.get_mut(&interface)
            {
                std::mem::replace(debug_requested, false)
            } else {
                false
            }
        }

        fn take_system_debug_requested(&self) -> bool {
            self.debug_info_requested.swap(false, std::sync::atomic::Ordering::SeqCst)
        }
    }

    #[derive(Debug)]
    struct MockDiagnostics {
        inner: MockState,
        interface: InterfaceId,
    }

    #[async_trait::async_trait]
    impl DeviceDiagnosticsProvider for MockDiagnostics {
        async fn get_counters(&self) -> Result<DeviceCounters, Error> {
            let Self { inner, interface } = self;
            let state = inner.lock().unwrap();
            state.get(interface).ok_or_else(|| Error::Fidl(fidl::Error::Invalid)).and_then(
                |MockCounterState { counters_result, debug_requested: _ }| {
                    counters_result.clone().expect("called get_counters on uninitialized mock")
                },
            )
        }

        async fn log_debug_info(&self) -> Result<(), Error> {
            let Self { inner, interface } = self;
            let mut state = inner.lock().unwrap();
            let MockCounterState { counters_result: _, debug_requested } =
                state.get_mut(interface).unwrap();
            *debug_requested = true;
            Ok(())
        }
    }
}
