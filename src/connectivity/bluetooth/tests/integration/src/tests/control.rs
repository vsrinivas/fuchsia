// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_test::{AdvertisingData, LowEnergyPeerParameters},
    fuchsia_bluetooth::{
        expectation::{
            asynchronous::{ExpectableState, ExpectableStateExt},
            Predicate,
        },
        hci_emulator::Emulator,
        types::Address,
    },
};

use crate::harness::control::{
    activate_fake_host, control_timeout, expectation, ControlHarness, ControlState,
    FAKE_HCI_ADDRESS,
};

async fn test_set_active_host(control: ControlHarness) -> Result<(), Error> {
    let initial_hosts: Vec<String> = control.read().hosts.keys().cloned().collect();
    let initial_hosts_ = initial_hosts.clone();

    let mut fake_hci_0 = Emulator::create_and_publish("bt-hci-integration-control-0").await?;
    let mut fake_hci_1 = Emulator::create_and_publish("bt-hci-integration-control-1").await?;

    let state = control
        .when_satisfied(
            Predicate::<ControlState>::new(
                move |control| {
                    let added_fake_hosts = control.hosts.iter().filter(|(id, host)| {
                        host.address == FAKE_HCI_ADDRESS && !initial_hosts_.contains(id)
                    });
                    added_fake_hosts.count() > 1
                },
                Some("Both Fake Hosts Added"),
            ),
            control_timeout(),
        )
        .await?;

    let fake_hosts: Vec<String> = state
        .hosts
        .iter()
        .filter(|(id, host)| host.address == FAKE_HCI_ADDRESS && !initial_hosts.contains(id))
        .map(|(id, _)| id.clone())
        .collect();

    for host in fake_hosts.iter() {
        let fut = control.aux().set_active_adapter(host);
        fut.await?;
        control
            .when_satisfied(expectation::active_host_is(host.to_string()), control_timeout())
            .await?;
    }

    fake_hci_0.destroy_and_wait().await?;
    fake_hci_1.destroy_and_wait().await?;

    for host in fake_hosts {
        control.when_satisfied(expectation::host_not_present(host), control_timeout()).await?;
    }

    Ok(())
}

async fn test_disconnect(control: ControlHarness) -> Result<(), Error> {
    let (_host, mut hci) = activate_fake_host(control.clone(), "bt-hci-integration").await?;

    // Insert a fake peer to test connection and disconnection.
    let peer_address = Address::Random([1, 0, 0, 0, 0, 0]);
    let peer_params = LowEnergyPeerParameters {
        address: Some(peer_address.into()),
        connectable: Some(true),
        advertisement: Some(AdvertisingData {
            data: vec![0x02, 0x01, 0x02], // Flags field set to "general discoverable"
        }),
        scan_response: None,
    };
    let (_peer, remote) = fidl::endpoints::create_proxy()?;
    let _ = hci
        .emulator()
        .add_low_energy_peer(peer_params, remote)
        .await?
        .map_err(|e| format_err!("Failed to register fake peer: {:#?}", e))?;

    let fut = control.aux().request_discovery(true);
    fut.await?;
    let state = control
        .when_satisfied(
            expectation::peer_with_address(&peer_address.to_string()),
            control_timeout(),
        )
        .await?;

    // TODO(armansito): Generalize Emulator/Peer state tracking from low_energy_peripheral tests and
    // verify the controller state here.

    // We can safely unwrap here as this is guarded by the previous expectation
    let peer = state.peers.iter().find(|(_, p)| &p.address == &peer_address.to_string()).unwrap().0;

    let fut = control.aux().connect(peer);
    fut.await?;

    control.when_satisfied(expectation::peer_connected(peer, true), control_timeout()).await?;
    let fut = control.aux().disconnect(peer);
    fut.await?;

    control.when_satisfied(expectation::peer_connected(peer, false), control_timeout()).await?;

    hci.destroy_and_wait().await?;
    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!("control.Control", [test_set_active_host, test_disconnect])
}
