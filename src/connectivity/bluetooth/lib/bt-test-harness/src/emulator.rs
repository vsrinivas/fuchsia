// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module defines common primitives for building and working with Harness types that rely on
/// interacting with bt-hci-emulator state.
///
/// There is no single 'EmulatorHarness' type; instead many different harness types can be built
/// that provide access to emulator behavior. To provide such harness functionality usually requires
/// two things:
///
///    - Providing access to a value of type `EmulatorState` within the Harness's State type, by
///      implementing `AsMut<EmulatorState>` on the state type
///    - Providing access to a fidl proxy of type `HciEmulatorProxy`, by implementing
///      `AsRef<HciEmulatorProxy>` on the auxilliary (Aux) type
///
/// This module defines the `EmulatorState` type, which represents the state common to the hci
/// emulator. It also provides common functionality for working with emulator behavior via this
/// state and via the HciEmulatorProxy.
///
/// The `expectation` submodule provides useful expectations (see `fuchsia_bluetooth::expectation`)
/// that can be used to write idiomatic testcases using these harnesses.
///
/// An example implementation of an Emulator harness may look like the following:
///
/// First, define our state type, nesting the `EmulatorState` within:
///
///     ```
///     #[derive(Clone, Debug, Default)]
///     pub struct PeripheralState {
///         emulator_state: EmulatorState,
///         connections: Vec<(Peer, ConnectionProxy)>,
///     }
///     ```
///
/// Then, define `AsMut` and `AsRef` implementations to provide access to the inner EmulatorState
///
///     ```
///     impl AsMut<EmulatorState> for PeripheralState {
///         fn as_mut(&mut self) -> &mut EmulatorState {
///             &mut self.emulator_state
///         }
///     }
///     impl AsRef<EmulatorState> for PeripheralState {
///         fn as_ref(&self) -> &EmulatorState {
///             &self.emulator_state
///         }
///     }
///     ```
///
/// Then, define an auxilliary type including the `HciEmulatorProxy`, and also implement `AsRef`:
///
///     ```
///     pub struct Aux {
///         peripheral: PeripheralProxy,
///         emulator: HciEmulatorProxy,
///     }
///     impl AsRef<HciEmulatorProxy> for Aux {
///         fn as_ref(&self) -> &HciEmulatorProxy {
///             &self.emulator
///         }
///     }
///     ```
///
/// Then we can build our harness by combining these two types:
///
///     ```
///     #[derive(Clone)]
///     pub struct PeripheralHarness(Expectable<PeripheralState, Aux>);
///     ```
///
/// Then we can use this harness to track emulator state and trigger expectations:
///
///     ```
///     // Start watching advertising events
///     let harness = PeripheralHarness::new(...);
///     fasync::Task::spawn(
///         watch_advertising_states(harness.deref().clone()).unwrap_or_else(|_| ())).detach();
///     let _ = harness.when_satisfied(emulator::expectation::advertising_is_enabled(true)).await?;
///     ```
use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth::{DeviceClass, MAJOR_DEVICE_CLASS_TOY},
    fidl_fuchsia_bluetooth_test::{
        AdvertisingData, BredrPeerParameters, ConnectionState, HciEmulatorProxy,
        LowEnergyPeerParameters, PeerProxy,
    },
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableExt, ExpectableState},
        types::{
            emulator::{ControllerParameters, LegacyAdvertisingState},
            Address,
        },
    },
    futures::{
        future::{err, Either},
        Future,
    },
    std::{
        collections::HashMap,
        convert::{AsMut, AsRef},
    },
};

/// Used to maintain the state transitions that are observed from the emulator. This type can be
/// used in test harness auxiliary types.
#[derive(Clone, Debug)]
pub struct EmulatorState {
    /// Most recently observed controller parameters.
    pub controller_parameters: Option<ControllerParameters>,

    /// Observed changes to the controller's advertising state and parameters.
    pub advertising_state_changes: Vec<LegacyAdvertisingState>,

    /// List of observed peer connection states.
    pub connection_states: HashMap<Address, Vec<ConnectionState>>,
}

impl Default for EmulatorState {
    fn default() -> EmulatorState {
        EmulatorState {
            controller_parameters: None,
            advertising_state_changes: vec![],
            connection_states: HashMap::new(),
        }
    }
}

pub fn default_le_peer(addr: &Address) -> LowEnergyPeerParameters {
    LowEnergyPeerParameters {
        address: Some(addr.into()),
        connectable: Some(true),
        advertisement: Some(AdvertisingData {
            data: vec![
                // Flags field set to "general discoverable"
                0x02, 0x01, 0x02, // Complete local name set to "Fake"
                0x05, 0x09, 'F' as u8, 'a' as u8, 'k' as u8, 'e' as u8,
            ],
        }),
        scan_response: None,
        ..LowEnergyPeerParameters::empty()
    }
}

/// An emulated BR/EDR peer using default parameters commonly used in tests. The peer is set up to
/// be connectable and has the "toy" device class.
pub fn default_bredr_peer(addr: &Address) -> BredrPeerParameters {
    BredrPeerParameters {
        address: Some(addr.into()),
        connectable: Some(true),
        device_class: Some(DeviceClass { value: MAJOR_DEVICE_CLASS_TOY }),
        service_definition: None,
        ..BredrPeerParameters::empty()
    }
}

pub fn add_le_peer(
    proxy: &HciEmulatorProxy,
    parameters: LowEnergyPeerParameters,
) -> impl Future<Output = Result<PeerProxy, Error>> {
    match fidl::endpoints::create_proxy() {
        Ok((local, remote)) => {
            let fut = proxy.add_low_energy_peer(parameters, remote);
            Either::Right(async {
                let _ = fut
                    .await?
                    .map_err(|e| format_err!("Failed to add emulated LE peer: {:?}", e))?;
                Ok(local)
            })
        }
        Err(e) => Either::Left(err(e.into())),
    }
}

pub fn add_bredr_peer(
    proxy: &HciEmulatorProxy,
    parameters: BredrPeerParameters,
) -> impl Future<Output = Result<PeerProxy, Error>> {
    match fidl::endpoints::create_proxy() {
        Ok((local, remote)) => {
            let fut = proxy.add_bredr_peer(parameters, remote);
            Either::Right(async {
                let _ = fut
                    .await?
                    .map_err(|e| format_err!("Failed to add emulated BR/EDR peer: {:?}", e))?;
                Ok(local)
            })
        }
        Err(e) => Either::Left(err(e.into())),
    }
}

pub async fn watch_controller_parameters<H, S, A>(harness: H) -> Result<(), Error>
where
    H: ExpectableState<State = S> + ExpectableExt<S, A>,
    S: AsMut<EmulatorState> + 'static,
    A: AsRef<HciEmulatorProxy>,
{
    let proxy = HciEmulatorProxy::clone(harness.aux().as_ref());
    loop {
        let cp = proxy.watch_controller_parameters().await?;
        harness.write_state().as_mut().controller_parameters = Some(cp.into());
        harness.notify_state_changed();
    }
}

/// Record advertising state changes. The asynchronous execution doesn't complete until the
/// emulator channel gets closed or a FIDL error occurs.
pub async fn watch_advertising_states<H, S, A>(harness: H) -> Result<(), Error>
where
    H: ExpectableState<State = S> + ExpectableExt<S, A>,
    S: AsMut<EmulatorState> + 'static,
    A: AsRef<HciEmulatorProxy>,
{
    let proxy = HciEmulatorProxy::clone(harness.aux().as_ref());
    loop {
        let states = proxy.watch_legacy_advertising_states().await?;
        harness
            .write_state()
            .as_mut()
            .advertising_state_changes
            .append(&mut states.into_iter().map(|s| s.into()).collect());
        harness.notify_state_changed();
    }
}

/// Record connection state changes from the given emulated Peer. The returned Future doesn't
/// run until the `proxy` channel gets closed or a FIDL error occurs.
pub async fn watch_peer_connection_states<H, S, A>(
    harness: H,
    address: Address,
    proxy: PeerProxy,
) -> Result<(), Error>
where
    H: ExpectableState<State = S> + ExpectableExt<S, A>,
    S: AsMut<EmulatorState> + 'static,
    A: AsRef<HciEmulatorProxy>,
{
    loop {
        let mut result = proxy.watch_connection_states().await?;
        // Introduce a scope as it is important not to hold a mutable lock to the harness state when
        // we call `harness.notify_state_changed()` below.
        {
            let mut s = harness.write_state();
            let state_map = &mut s.as_mut().connection_states;
            if !state_map.contains_key(&address) {
                state_map.insert(address, vec![]);
            }
            let states = state_map.get_mut(&address).unwrap();
            states.append(&mut result);
        }
        harness.notify_state_changed();
    }
}

/// Utilities used for setting up expectation predicates on the HCI emulator state transitions.
pub mod expectation {
    use {
        super::*,
        fidl_fuchsia_bluetooth::DeviceClass,
        fidl_fuchsia_bluetooth_test::{ConnectionState, LegacyAdvertisingType},
        fuchsia_bluetooth::{expectation::Predicate, types::Address},
    };

    pub fn local_name_is<S>(name: &'static str) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        Predicate::equal(Some(name.to_string())).over_value(
            |state: &S| {
                state
                    .as_ref()
                    .controller_parameters
                    .as_ref()
                    .and_then(|p| p.local_name.as_ref().map(|o| o.to_string()))
            },
            "controller_parameters.local_name",
        )
    }

    pub fn device_class_is<S>(device_class: DeviceClass) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        Predicate::equal(Some(device_class)).over_value(
            |state: &S| state.as_ref().controller_parameters.as_ref().and_then(|p| p.device_class),
            "controller_parameters.device_class",
        )
    }

    pub fn advertising_is_enabled<S>(enabled: bool) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        Predicate::equal(Some(enabled)).over_value(
            |state: &S| state.as_ref().advertising_state_changes.last().map(|s| s.enabled),
            "controller_parameters.device_class",
        )
    }

    pub fn advertising_was_enabled<S>(enabled: bool) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        let descr = format!("advertising was (enabled: {})", enabled);
        Predicate::predicate(
            move |state: &S| -> bool {
                state.as_ref().advertising_state_changes.iter().any(|s| s.enabled == enabled)
            },
            &descr,
        )
    }

    pub fn advertising_type_is<S>(type_: LegacyAdvertisingType) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        let descr = format!("advertising type is: {:#?}", type_);
        Predicate::predicate(
            move |state: &S| -> bool {
                state
                    .as_ref()
                    .advertising_state_changes
                    .last()
                    .and_then(|s| s.type_)
                    .map_or(false, |t| t == type_)
            },
            &descr,
        )
    }

    pub fn advertising_data_is<S>(data: Vec<u8>) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        let descr = format!("advertising data is: {:#?}", data);
        Predicate::predicate(
            move |state: &S| -> bool {
                state
                    .as_ref()
                    .advertising_state_changes
                    .last()
                    .and_then(|s| s.advertising_data.as_ref())
                    .map_or(false, |a| *a == data)
            },
            &descr,
        )
    }

    pub fn scan_response_is<S>(data: Vec<u8>) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        let descr = format!("scan response data is: {:#?}", data);
        Predicate::predicate(
            move |state: &S| -> bool {
                state
                    .as_ref()
                    .advertising_state_changes
                    .last()
                    .and_then(|s| s.scan_response.as_ref())
                    .map_or(false, |s| *s == data)
            },
            &descr,
        )
    }

    fn to_slices(ms: u16) -> u16 {
        let slices = (ms as u32) * 1000 / 625;
        slices as u16
    }

    pub fn advertising_max_interval_is<S>(interval_ms: u16) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        let descr = format!("advertising max interval is: {:#?} ms", interval_ms);
        Predicate::predicate(
            move |state: &S| -> bool {
                state
                    .as_ref()
                    .advertising_state_changes
                    .last()
                    .and_then(|s| s.interval_max)
                    .map_or(false, |i| i == to_slices(interval_ms))
            },
            &descr,
        )
    }

    pub fn peer_connection_state_was<S>(address: Address, state: ConnectionState) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        let descr = format!("emulated peer connection state was: {:?}", state);
        Predicate::predicate(
            move |s: &S| -> bool {
                s.as_ref().connection_states.get(&address).map_or(false, |s| s.contains(&state))
            },
            &descr,
        )
    }

    pub fn peer_connection_state_is<S>(address: Address, state: ConnectionState) -> Predicate<S>
    where
        S: 'static + AsRef<EmulatorState>,
    {
        let descr = format!("emulated peer connection state is: {:?}", state);
        Predicate::predicate(
            move |s: &S| -> bool {
                s.as_ref()
                    .connection_states
                    .get(&address)
                    .map_or(false, |s| s.last() == Some(&state))
            },
            &descr,
        )
    }
}
