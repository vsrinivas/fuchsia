// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_bluetooth::{
        expectation::{asynchronous::ExpectableStateExt, Predicate},
        fake_hci::FakeHciDevice,
    },
};

use crate::harness::control::{control_expectation, control_timeout, ControlHarness, ControlState};

pub async fn set_active_host(control: ControlHarness) -> Result<(), Error> {
    let _hci_1 = FakeHciDevice::new()?;
    let _hci_2 = FakeHciDevice::new()?;

    let state = await!(control.when_satisfied(
        Predicate::<ControlState>::new(|control|
            control.hosts.iter()
            .filter(|(_,h)| h.address == "00:00:00:00:00:00").count() > 1, None),
        control_timeout()
    ))?;

    for (id, _) in state.hosts {
        await!(control.aux().set_active_adapter(&id))?;
        await!(control.when_satisfied(control_expectation::active_host_is(id), control_timeout()))?;
    }
    Ok(())
}
