// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_bluetooth::{
        expectation::{asynchronous::{ExpectableState, ExpectableStateExt}, Predicate},
        fake_hci::FakeHciDevice,
    },
};

use crate::harness::{
    low_energy_central::FAKE_HCI_ADDRESS,
    control::{control_expectation, control_timeout, ControlHarness, ControlState},
};

pub async fn set_active_host(control: ControlHarness) -> Result<(), Error> {
    let initial_hosts: Vec<String> = control.read().hosts.keys().cloned().collect();
    let initial_hosts_ = initial_hosts.clone();

    let fake_hci_1 = FakeHciDevice::new()?;
    let fake_hci_2 = FakeHciDevice::new()?;

    let state = await!(control.when_satisfied(
        Predicate::<ControlState>::new(move |control|
            control.hosts.iter()
            .filter(|(id,host)| host.address == FAKE_HCI_ADDRESS && !initial_hosts_.contains(id)).count() > 1, None),
        control_timeout()
    ))?;

    let fake_hosts: Vec<String> = state.hosts.iter().filter(|(id,host)| host.address == FAKE_HCI_ADDRESS && !initial_hosts.contains(id)).map(|(id,_)| id).cloned().collect();

    for (id, _) in state.hosts {
        await!(control.aux().set_active_adapter(&id))?;
        await!(control.when_satisfied(control_expectation::active_host_is(id), control_timeout()))?;
    }

    drop(fake_hci_1);
    drop(fake_hci_2);

    for host in fake_hosts {
        await!(control.when_satisfied(control_expectation::host_not_present(host), control_timeout()))?;
    }

    Ok(())
}
