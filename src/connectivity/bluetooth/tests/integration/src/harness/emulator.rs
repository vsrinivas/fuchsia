// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The `emulator` module defines common primitives for test harnesses that rely on interacting with
/// bt-hci-emulator state. As all test harnesses are composed of a mutable state type and auxiliary
/// data, the following types are defined to enable access to emulator state by composition:
///
///     - `EmulatorState`: Reflects the state of the underlying controller.
///     - `EmulatorHarnessAux`: Combines an HciEmulator FIDL proxy with arbitrary auxiliary data.
///
/// A custom test harness can then be formed by combining these types like so:
///
///     pub struct FooState {
///         pub foo_state: Foo,
///         emulator_state: EmulatorState,
///     }
///
///     // A test harness that has expectations on FooState and often accesses a FooProxy:
///     pub type FooHarness = ExpectationHarness<FooState, EmulatorHarnessAux<FooProxy>>;
///
/// The `emulator` module also defines functions to watch emulator state and to build expectations
/// over them. The following traits are defined to allow these utilities access to the emulator
/// state:
///
///     - `EmulatorHarnessState`: Must provide mutable and immutable access to an `EmulatorState`.
///        This trait is automatically implemented for all types that implement `AsRef<EmulatorState>`
///        and `AsMut<EmulatorState>`.
///     - `EmulatorHarness`: Must provide access to a HciEmulator FIDL proxy and the test harness
///       state.
///
/// By implementing these traits, a harness can build predicates that can be combined with its own state.
/// For example:
///
///     impl AsRef<EmulatorState> for FooState {
///         fn as_ref(&self) -> &EmulatorState {
///             &self.emulator_state
///         }
///     }
///
///     impl AsMut<EmulatorState> for FooState {
///         fn as_mut(&mut self) -> &mut EmulatorState {
///             &mut self.emulator_state
///         }
///     }
///
///     impl EmulatorHarness for PeripheralHarness {
///         type State = FooState;
///
///         fn emulator(&self) -> HciEmulatorProxy {
///             self.aux().emulator().clone()
///         }
///         fn state(&self) -> MappedRwLockWriteGuard<FooState> {
///             self.write_state()
///         }
///     }
///
///     ...
///     use crate::harness::emulator;
///
///     // Start watching advertising events
///     let harness = FooHarness::new(...);
///     fasync::Task::spawn(watch_advertising_states(harness.clone()).unwrap_or_else(|_| ())).detach();
///     let _ = harness.when_satisfied(emulator::expectation::advertising_is_enabled(true)).await?;
use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth::{DeviceClass, MAJOR_DEVICE_CLASS_TOY},
    fidl_fuchsia_bluetooth_test::{
        AdvertisingData, BredrPeerParameters, ConnectionState, HciEmulatorProxy,
        LowEnergyPeerParameters, PeerProxy,
    },
    fuchsia_bluetooth::{
        expectation::asynchronous::ExpectableState,
        types::{
            emulator::{ControllerParameters, LegacyAdvertisingState},
            Address,
        },
    },
    futures::Future,
    parking_lot::MappedRwLockWriteGuard,
    std::collections::HashMap,
    std::convert::{AsMut, AsRef},
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

/// Harness auxiliary type that carries a generic FIDL proxy and a bt-emulator FIDL proxy. This is
/// useful for tests that exercise a FIDL interface against programmable emulator functionality.
pub struct EmulatorHarnessAux<T> {
    proxy: T,
    emulator: HciEmulatorProxy,
}

impl<T> EmulatorHarnessAux<T> {
    pub fn new(proxy: T, emulator: HciEmulatorProxy) -> EmulatorHarnessAux<T> {
        EmulatorHarnessAux { proxy, emulator }
    }

    pub fn proxy(&self) -> &T {
        &self.proxy
    }

    pub fn emulator(&self) -> &HciEmulatorProxy {
        &self.emulator
    }

    pub fn add_le_peer(
        &self,
        parameters: LowEnergyPeerParameters,
    ) -> impl Future<Output = Result<PeerProxy, Error>> {
        // We pass the proxy to a helper function by value so that there is no need to capture
        // `self` (which is a borrow) into the Future, which prevents lifetime issues with `await`
        // and a temporary `self`.
        add_le_peer(self.emulator.clone(), parameters)
    }

    /// Set up an emulated LE peer using default parameters commonly used in tests. The peer is set
    /// up to be connectable and advertises the name "Fake".
    pub fn add_le_peer_default(
        &self,
        address: &Address,
    ) -> impl Future<Output = Result<PeerProxy, Error>> {
        let parameters = LowEnergyPeerParameters {
            address: Some(address.into()),
            connectable: Some(true),
            advertisement: Some(AdvertisingData {
                data: vec![
                    // Flags field set to "general discoverable"
                    0x02, 0x01, 0x02, // Complete local name set to "Fake"
                    0x05, 0x09, 'F' as u8, 'a' as u8, 'k' as u8, 'e' as u8,
                ],
            }),
            scan_response: None,
        };
        self.add_le_peer(parameters)
    }

    pub fn add_bredr_peer(
        &self,
        parameters: BredrPeerParameters,
    ) -> impl Future<Output = Result<PeerProxy, Error>> {
        // We pass the proxy to a helper function by value so that there is no need to capture
        // `self` (which is a borrow) into the Future, which prevents lifetime issues with `await`
        // and a temporary `self`.
        add_bredr_peer(self.emulator.clone(), parameters)
    }

    /// Set up an emulated LE peer using default parameters commonly used in tests. The peer is set
    /// up to be connectable and has the "toy" device class.
    pub fn add_bredr_peer_default(
        &self,
        address: &Address,
    ) -> impl Future<Output = Result<PeerProxy, Error>> {
        let parameters = BredrPeerParameters {
            address: Some(address.into()),
            connectable: Some(true),
            device_class: Some(DeviceClass { value: MAJOR_DEVICE_CLASS_TOY }),
            service_definition: None,
        };
        self.add_bredr_peer(parameters)
    }
}

async fn add_le_peer(
    proxy: HciEmulatorProxy,
    parameters: LowEnergyPeerParameters,
) -> Result<PeerProxy, Error> {
    let (local, remote) = fidl::endpoints::create_proxy()?;
    let _ = proxy
        .add_low_energy_peer(parameters, remote)
        .await?
        .map_err(|e| format_err!("Failed to add emulated LE peer: {:?}", e))?;
    Ok(local)
}

async fn add_bredr_peer(
    proxy: HciEmulatorProxy,
    parameters: BredrPeerParameters,
) -> Result<PeerProxy, Error> {
    let (local, remote) = fidl::endpoints::create_proxy()?;
    let _ = proxy
        .add_bredr_peer(parameters, remote)
        .await?
        .map_err(|e| format_err!("Failed to add emulated BR/EDR peer: {:?}", e))?;
    Ok(local)
}

/// Trait that allows a generic test harness state to provide an `EmulatorState`. This trait is
/// automatically implemented for types that implement the declared trait bounds.
pub trait EmulatorHarnessState: AsMut<EmulatorState> + AsRef<EmulatorState> {}
impl<T> EmulatorHarnessState for T where T: AsMut<EmulatorState> + AsRef<EmulatorState> {}

pub trait EmulatorHarness: ExpectableState + Send + Sync {
    type State: EmulatorHarnessState;

    /// Return a FIDL proxy to the emulator to observe state updates from.
    fn emulator(&self) -> HciEmulatorProxy;

    /// Return a guarded reference to the state object that implements `EmulatorHarnessState`.
    fn state(&self) -> MappedRwLockWriteGuard<'_, <Self as EmulatorHarness>::State>;
}

/// Watch for updates to controller parameters and record the latest snapshot. The asynchronous
/// execution doesn't complete until the emulator channel gets closed or a FIDL error occurs.
pub async fn watch_controller_parameters<H>(harness: H) -> Result<(), Error>
where
    H: EmulatorHarness,
{
    loop {
        let cp = harness.emulator().watch_controller_parameters().await?;
        harness.state().as_mut().controller_parameters = Some(cp.into());
        harness.notify_state_changed();
    }
}

/// Record advertising state changes. The asynchronous execution doesn't complete until the
/// emulator channel gets closed or a FIDL error occurs.
pub async fn watch_advertising_states<H>(harness: H) -> Result<(), Error>
where
    H: EmulatorHarness,
{
    loop {
        let states = harness.emulator().watch_legacy_advertising_states().await?;
        harness
            .state()
            .as_mut()
            .advertising_state_changes
            .append(&mut states.into_iter().map(|s| s.into()).collect());
        harness.notify_state_changed();
    }
}

/// Record connection state changes from the given emulated Peer. The returned Future doesn't
/// run until the `proxy` channel gets closed or a FIDL error occurs.
pub async fn watch_peer_connection_states<H>(
    harness: H,
    address: Address,
    proxy: PeerProxy,
) -> Result<(), Error>
where
    H: EmulatorHarness,
{
    loop {
        let mut result = proxy.watch_connection_states().await?;
        // Introduce a scope as it is important not to hold a mutable lock to the harness state when
        // we call `harness.notify_state_changed()` below.
        {
            let mut s = harness.state();
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
        super::EmulatorHarnessState,
        fidl_fuchsia_bluetooth::DeviceClass,
        fidl_fuchsia_bluetooth_test::{ConnectionState, LegacyAdvertisingType},
        fuchsia_bluetooth::{expectation::Predicate, types::Address},
    };

    pub fn local_name_is<S>(name: &'static str) -> Predicate<S>
    where
        S: 'static + EmulatorHarnessState,
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
        S: 'static + EmulatorHarnessState,
    {
        Predicate::equal(Some(device_class)).over_value(
            |state: &S| state.as_ref().controller_parameters.as_ref().and_then(|p| p.device_class),
            "controller_parameters.device_class",
        )
    }

    pub fn advertising_is_enabled<S>(enabled: bool) -> Predicate<S>
    where
        S: 'static + EmulatorHarnessState,
    {
        Predicate::equal(Some(enabled)).over_value(
            |state: &S| state.as_ref().advertising_state_changes.last().map(|s| s.enabled),
            "controller_parameters.device_class",
        )
    }

    pub fn advertising_was_enabled<S>(enabled: bool) -> Predicate<S>
    where
        S: 'static + EmulatorHarnessState,
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
        S: 'static + EmulatorHarnessState,
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
        S: 'static + EmulatorHarnessState,
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
        S: 'static + EmulatorHarnessState,
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
        S: 'static + EmulatorHarnessState,
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
        S: 'static + EmulatorHarnessState,
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
        S: 'static + EmulatorHarnessState,
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
