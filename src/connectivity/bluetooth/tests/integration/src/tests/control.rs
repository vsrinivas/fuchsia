// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_bluetooth::{
        expectation::{
            asynchronous::{ExpectableState, ExpectableStateExt},
            Predicate,
        },
        hci_emulator::Emulator,
    },
};

use crate::harness::control::{
    control_expectation, control_timeout, ControlHarness, ControlState, FAKE_HCI_ADDRESS,
};

pub async fn set_active_host(control: ControlHarness) -> Result<(), Error> {
    let initial_hosts: Vec<String> = control.read().hosts.keys().cloned().collect();
    let initial_hosts_ = initial_hosts.clone();

    let fake_hci_0 = await!(Emulator::new("bt-hci-integration-control-0"))?;
    let fake_hci_1 = await!(Emulator::new("bt-hci-integration-control-1"))?;

    let state = await!(control.when_satisfied(
        Predicate::<ControlState>::new(
            move |control| {
                let added_fake_hosts = control.hosts.iter().filter(|(id, host)| {
                    host.address == FAKE_HCI_ADDRESS && !initial_hosts_.contains(id)
                });
                added_fake_hosts.count() > 1
            },
            Some("Both Fake Hosts Added")
        ),
        control_timeout()
    ))?;

    let fake_hosts: Vec<String> = state
        .hosts
        .iter()
        .filter(|(id, host)| host.address == FAKE_HCI_ADDRESS && !initial_hosts.contains(id))
        .map(|(id, _)| id)
        .cloned()
        .collect();

    for (id, _) in state.hosts {
        await!(control.aux().set_active_adapter(&id))?;
        await!(control.when_satisfied(control_expectation::active_host_is(id), control_timeout()))?;
    }

    drop(fake_hci_0);
    drop(fake_hci_1);

    for host in fake_hosts {
        await!(
            control.when_satisfied(control_expectation::host_not_present(host), control_timeout())
        )?;
    }

    Ok(())
}
