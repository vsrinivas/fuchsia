// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_bluetooth::{
        expectation::{
            self,
            asynchronous::{ExpectableState, ExpectableStateExt},
            Predicate,
        },
        hci_emulator::Emulator,
    },
};

use crate::harness::control::{
    activate_fake_host, control_expectation, control_timeout, ControlHarness, ControlState,
    FAKE_HCI_ADDRESS,
};

// TODO(BT-229): Currently these tests rely on fakes that are hard-coded in the fake
// HCI driver. Remove these once it is possible to set up mock devices programmatically.
const FAKE_LE_DEVICE_ADDR: &str = "00:00:00:00:00:01";

pub async fn set_active_host(control: ControlHarness) -> Result<(), Error> {
    let initial_hosts: Vec<String> = control.read().hosts.keys().cloned().collect();
    let initial_hosts_ = initial_hosts.clone();

    let fake_hci_0 = Emulator::create_and_publish("bt-hci-integration-control-0").await?;
    let fake_hci_1 = Emulator::create_and_publish("bt-hci-integration-control-1").await?;

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
        .map(|(id, _)| id)
        .cloned()
        .collect();

    for (id, _) in state.hosts {
        control.aux().set_active_adapter(&id).await?;
        control.when_satisfied(control_expectation::active_host_is(id), control_timeout()).await?;
    }

    drop(fake_hci_0);
    drop(fake_hci_1);

    for host in fake_hosts {
        control
            .when_satisfied(control_expectation::host_not_present(host), control_timeout())
            .await?;
    }

    Ok(())
}

pub async fn disconnect(control: ControlHarness) -> Result<(), Error> {
    let (_host, _hci) = activate_fake_host(control.clone(), "bt-hci-integration").await?;

    control.aux().request_discovery(true).await?;
    let state = control
        .when_satisfied(
            control_expectation::peer_exists(expectation::peer::address(FAKE_LE_DEVICE_ADDR)),
            control_timeout(),
        )
        .await?;

    // We can safely unwrap here as this is guarded by the previous expectation
    let peer = state.peers.iter().find(|(_, d)| &d.address == FAKE_LE_DEVICE_ADDR).unwrap().0;

    control.aux().connect(peer).await?;

    control
        .when_satisfied(control_expectation::peer_connected(peer, true), control_timeout())
        .await?;
    control.aux().disconnect(peer).await?;

    control
        .when_satisfied(control_expectation::peer_connected(peer, false), control_timeout())
        .await?;
    Ok(())
}
