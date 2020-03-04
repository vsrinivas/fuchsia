// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_sys::ProcedureTokenProxy,
    fidl_fuchsia_bluetooth_test::{AdvertisingData, LowEnergyPeerParameters, PeerProxy},
    fuchsia_bluetooth::{
        expectation::asynchronous::ExpectableStateExt,
        hci_emulator::Emulator,
        types::{Address, HostId},
    },
    std::str::FromStr,
};

use crate::{
    harness::{
        access::AccessHarness,
        control::{activate_fake_host, ControlHarness},
        host_watcher::HostWatcherHarness,
    },
    tests::timeout_duration,
};

async fn create_le_peer(hci: &Emulator, address: Address) -> Result<PeerProxy, Error> {
    let peer_params = LowEnergyPeerParameters {
        address: Some(address.into()),
        connectable: Some(true),
        advertisement: Some(AdvertisingData {
            data: vec![0x02, 0x01, 0x02], // Flags field set to "general discoverable"
        }),
        scan_response: None,
    };

    let (peer, remote) = fidl::endpoints::create_proxy()?;
    let _ = hci
        .emulator()
        .add_low_energy_peer(peer_params, remote)
        .await?
        .map_err(|e| format_err!("Failed to register fake peer: {:#?}", e))?;
    Ok(peer)
}

async fn start_discovery(access: &AccessHarness) -> Result<ProcedureTokenProxy, Error> {
    // We create a capability to capture the discovery token, and pass it to the access provider
    // Discovery will drop once we drop this token
    let (token, token_server) = fidl::endpoints::create_proxy()?;
    let fidl_response = access.aux().start_discovery(token_server);
    fidl_response
        .await?
        .map_err(|sys_err| format_err!("Error calling StartDiscovery(): {:?}", sys_err))?;
    Ok(token)
}

async fn make_discoverable(access: &AccessHarness) -> Result<ProcedureTokenProxy, Error> {
    // We create a capability to capture the discoverable token, and pass it to the access provider
    // Discoverable will drop once we drop this token
    let (token, token_server) = fidl::endpoints::create_proxy()?;
    let fidl_response = access.aux().make_discoverable(token_server);
    fidl_response
        .await?
        .map_err(|sys_err| format_err!("Error calling StartDiscoverable(): {:?}", sys_err))?;
    Ok(token)
}

// Test that we can
//  * Enable discovery via fuchsia.bluetooth.sys.Access.StartDiscovery()
//  * Receive peer information via fuchsia.bluetooth.sys.Access.WatchPeers()
async fn test_watch_peers((access, control): (AccessHarness, ControlHarness)) -> Result<(), Error> {
    let (_host, mut hci) = activate_fake_host(control, "bt-hci-integration").await?;

    let first_address = Address::Random([1, 0, 0, 0, 0, 0]);
    let second_address = Address::Public([2, 0, 0, 0, 0, 0]);
    let _first_peer = create_le_peer(&hci, first_address).await?;

    let _discovery_token = start_discovery(&access).await?;

    // We should be notified of the first peer
    let state = access
        .when_satisfied(expectation::peer_with_address(first_address), timeout_duration())
        .await?;

    // We should not have seen the second peer yet
    assert!(!expectation::peer_with_address(second_address).satisfied(&state));

    // Once the second peer is added, we should see it
    let _second_peer = create_le_peer(&hci, second_address).await?;
    let _state = access
        .when_satisfied(expectation::peer_with_address(second_address), timeout_duration())
        .await?;

    hci.destroy_and_wait().await?;
    Ok(())
}

async fn test_disconnect((access, control): (AccessHarness, ControlHarness)) -> Result<(), Error> {
    let (_host, mut hci) = activate_fake_host(control.clone(), "bt-hci-integration").await?;

    let peer_address = Address::Random([6, 5, 0, 0, 0, 0]);
    let _peer = create_le_peer(&hci, peer_address).await?;

    let _discovery = start_discovery(&access).await?;

    let state = access
        .when_satisfied(expectation::peer_with_address(peer_address), timeout_duration())
        .await?;

    // We can safely unwrap here as this is guarded by the previous expectation
    let peer_id = state.peers.values().find(|p| p.address == peer_address).unwrap().id;

    let fidl_response = access.aux().connect(&mut peer_id.into());
    fidl_response
        .await?
        .map_err(|sys_err| format_err!("Error calling Connect(): {:?}", sys_err))?;

    access.when_satisfied(expectation::peer_connected(peer_id, true), timeout_duration()).await?;

    let fidl_response = access.aux().disconnect(&mut peer_id.into());
    fidl_response
        .await?
        .map_err(|sys_err| format_err!("Error calling Disconnect(): {:?}", sys_err))?;

    access.when_satisfied(expectation::peer_connected(peer_id, false), timeout_duration()).await?;

    hci.destroy_and_wait().await?;
    Ok(())
}

// Test that we can
//  * Set local name via fuchsia.bluetooth.sys.Access.SetLocalName()
//  * Receive host information via fuchsia.bluetooth.sys.HostWatcher.Watch()
async fn test_set_local_name(
    (access, (control, host_watcher)): (AccessHarness, (ControlHarness, HostWatcherHarness)),
) -> Result<(), Error> {
    let (_host, mut hci) = activate_fake_host(control.clone(), "bt-hci-integration").await?;

    host_watcher.when_satisfied(expectation::host_with_name("fuchsia"), timeout_duration()).await?;

    let expected_name = "bt-integration-test";
    access.aux().set_local_name(expected_name)?;

    host_watcher
        .when_satisfied(expectation::host_with_name(expected_name), timeout_duration())
        .await?;

    hci.destroy_and_wait().await?;
    Ok(())
}

// Test that we can
//  * Enable discovery via fuchsia.bluetooth.sys.Access.StartDiscovery()
//  * Disable discovery by dropping our token
async fn test_discovery(
    (access, (control, host_watcher)): (AccessHarness, (ControlHarness, HostWatcherHarness)),
) -> Result<(), Error> {
    let (host, mut hci) = activate_fake_host(control.clone(), "bt-hci-integration").await?;
    let host = HostId::from_str(&host)?;

    let discovery_token = start_discovery(&access).await?;

    // We should now be discovering
    host_watcher
        .when_satisfied(expectation::host_discovering(host, true), timeout_duration())
        .await?;

    // Drop our end of the token channel
    std::mem::drop(discovery_token);

    // Since no-one else has requested discovery, we should cease discovery
    host_watcher
        .when_satisfied(expectation::host_discovering(host, false), timeout_duration())
        .await?;

    hci.destroy_and_wait().await?;
    Ok(())
}

// Test that we can
//  * Enable discoverable via fuchsia.bluetooth.sys.Access.StartDiscoverable()
//  * Disable discoverable by dropping our token
async fn test_discoverable(
    (access, (control, host_watcher)): (AccessHarness, (ControlHarness, HostWatcherHarness)),
) -> Result<(), Error> {
    let (host, mut hci) = activate_fake_host(control.clone(), "bt-hci-integration").await?;
    let host = HostId::from_str(&host)?;

    let discoverable_token = make_discoverable(&access).await?;

    // We should now be discoverable
    host_watcher
        .when_satisfied(expectation::host_discoverable(host, true), timeout_duration())
        .await?;

    // Drop our end of the token channel
    std::mem::drop(discoverable_token);

    // Since no-one else has requested discoverable, we should cease discoverable
    host_watcher
        .when_satisfied(expectation::host_discoverable(host, false), timeout_duration())
        .await?;

    hci.destroy_and_wait().await?;
    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!(
        "sys.Access",
        [test_watch_peers, test_disconnect, test_set_local_name, test_discoverable, test_discovery]
    )
}

pub mod expectation {
    use crate::harness::{access::AccessState, host_watcher::HostWatcherState};
    use fuchsia_bluetooth::{
        expectation::Predicate,
        types::{Address, HostId, HostInfo, Peer, PeerId},
    };

    mod peer {
        use super::*;

        pub(crate) fn exists(p: Predicate<Peer>) -> Predicate<AccessState> {
            let msg = format!("peer exists satisfying {}", p.describe());
            Predicate::new(
                move |state: &AccessState| state.peers.iter().any(|(_, d)| p.satisfied(d)),
                Some(&msg),
            )
        }

        pub(crate) fn with_identifier(id: PeerId) -> Predicate<Peer> {
            Predicate::<Peer>::new(move |d| d.id == id, Some(&format!("identifier == {}", id)))
        }

        pub(crate) fn with_address(address: Address) -> Predicate<Peer> {
            Predicate::<Peer>::new(
                move |d| d.address == address,
                Some(&format!("address == {}", address)),
            )
        }

        pub(crate) fn connected(connected: bool) -> Predicate<Peer> {
            Predicate::<Peer>::new(
                move |d| d.connected == connected,
                Some(&format!("connected == {}", connected)),
            )
        }
    }

    mod host {
        use super::*;

        pub(crate) fn with_name<S: ToString>(name: S) -> Predicate<HostInfo> {
            let name = name.to_string();
            let msg = format!("name == {}", name);
            Predicate::<HostInfo>::new(move |h| h.local_name.as_ref() == Some(&name), Some(&msg))
        }

        pub(crate) fn with_id(id: HostId) -> Predicate<HostInfo> {
            let msg = format!("id == {}", id);
            Predicate::<HostInfo>::new(move |h| h.id == id, Some(&msg))
        }

        pub(crate) fn discovering(is_discovering: bool) -> Predicate<HostInfo> {
            let msg = format!("discovering == {}", is_discovering);
            Predicate::<HostInfo>::new(move |h| h.discovering == is_discovering, Some(&msg))
        }

        pub(crate) fn discoverable(is_discoverable: bool) -> Predicate<HostInfo> {
            let msg = format!("discoverable == {}", is_discoverable);
            Predicate::<HostInfo>::new(move |h| h.discoverable == is_discoverable, Some(&msg))
        }

        pub(crate) fn exists(p: Predicate<HostInfo>) -> Predicate<HostWatcherState> {
            let msg = format!("Host exists satisfying {}", p.describe());
            Predicate::new(
                move |state: &HostWatcherState| state.hosts.values().any(|h| p.satisfied(h)),
                Some(&msg),
            )
        }
    }

    pub fn peer_connected(id: PeerId, connected: bool) -> Predicate<AccessState> {
        peer::exists(peer::with_identifier(id).and(peer::connected(connected)))
    }

    pub fn peer_with_address(address: Address) -> Predicate<AccessState> {
        peer::exists(peer::with_address(address))
    }

    pub fn host_with_name<S: ToString>(name: S) -> Predicate<HostWatcherState> {
        host::exists(host::with_name(name))
    }

    pub fn host_discovering(id: HostId, is_discovering: bool) -> Predicate<HostWatcherState> {
        host::exists(host::with_id(id).and(host::discovering(is_discovering)))
    }
    pub fn host_discoverable(id: HostId, is_discoverable: bool) -> Predicate<HostWatcherState> {
        host::exists(host::with_id(id).and(host::discoverable(is_discoverable)))
    }
}
