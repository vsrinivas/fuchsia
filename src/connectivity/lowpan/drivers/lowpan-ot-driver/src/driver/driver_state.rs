// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use fidl_fuchsia_lowpan::{ConnectivityState, Identity, Role};
use fidl_fuchsia_lowpan_device::DeviceState;

use lowpan_driver_common::spinel::AddressTable;

#[derive(Debug)]
pub struct DriverState<OT> {
    pub ot_instance: OT,

    pub connectivity_state: ConnectivityState,

    /// The current set of addresses configured for this interface.
    pub(super) address_table: AddressTable,

    pub srp_discovery_proxy: Option<DiscoveryProxy>,
}

impl<OT: AsRef<ot::Instance>> AsRef<ot::Instance> for DriverState<OT> {
    fn as_ref(&self) -> &ot::Instance {
        self.ot_instance.as_ref()
    }
}

impl<OT> AsRef<Option<DiscoveryProxy>> for DriverState<OT> {
    fn as_ref(&self) -> &Option<DiscoveryProxy> {
        &self.srp_discovery_proxy
    }
}

impl<OT: AsRef<ot::Instance>> DriverState<OT> {
    pub fn is_discovery_proxy_enabled(&self) -> bool {
        self.srp_discovery_proxy.is_some()
    }

    pub fn set_discovery_proxy_enabled(&mut self, enabled: bool) -> Result {
        if self.is_discovery_proxy_enabled() != enabled {
            let _ = self.srp_discovery_proxy.take();
            if enabled {
                self.srp_discovery_proxy =
                    Some(DiscoveryProxy::new(AsRef::<ot::Instance>::as_ref(self))?);
            }
        }
        Ok(())
    }
}

impl<OT> DriverState<OT> {
    pub fn new(ot_instance: OT) -> Self {
        DriverState {
            ot_instance,
            connectivity_state: ConnectivityState::Inactive,
            address_table: Default::default(),
            srp_discovery_proxy: None,
        }
    }
}

impl<OT> DriverState<OT>
where
    OT: Send + ot::InstanceInterface,
{
    #[allow(dead_code)]
    pub fn is_ready(&self) -> bool {
        self.connectivity_state.is_ready()
    }

    #[allow(dead_code)]
    pub fn is_active(&self) -> bool {
        self.connectivity_state.is_active()
    }

    pub fn is_active_and_ready(&self) -> bool {
        self.connectivity_state.is_active_and_ready()
    }

    pub fn is_commissioning(&self) -> bool {
        self.connectivity_state.is_commissioning()
    }
}

impl<OT> DriverState<OT>
where
    OT: Send + ot::InstanceInterface,
{
    pub fn is_busy(&self) -> bool {
        self.ot_instance.is_energy_scan_in_progress()
            || self.ot_instance.is_active_scan_in_progress()
            || self.ot_instance.joiner_get_state() != ot::JoinerState::Idle
    }

    pub fn updated_connectivity_state(&self) -> ConnectivityState {
        let mut ret = self.connectivity_state;

        if self.ot_instance.is_commissioned() {
            ret = ret.provisioned();
        } else if self.ot_instance.joiner_get_state() == ot::JoinerState::Idle {
            ret = ret.unprovisioned();
        } else {
            ret = ret.commissioning().unwrap();
        }

        let role = self.get_current_role();

        ret = ret.role_updated(role);

        ret
    }

    pub fn get_current_role(&self) -> Role {
        match self.ot_instance.get_device_role() {
            ot::DeviceRole::Disabled => Role::Detached,
            ot::DeviceRole::Detached => Role::Detached,
            ot::DeviceRole::Child => Role::EndDevice,
            ot::DeviceRole::Router => Role::Router,
            ot::DeviceRole::Leader => Role::Leader,
        }
    }

    pub fn get_current_identity(&self) -> Identity {
        if !self.ot_instance.is_commissioned() {
            return Identity::EMPTY;
        }

        let mut operational_dataset = Default::default();
        match self.ot_instance.dataset_get_active(&mut operational_dataset) {
            Ok(()) => operational_dataset.into_ext(),
            Err(err) => {
                warn!("Commissioned, but unable to get active dataset: {:?}", err);
                Identity::EMPTY
            }
        }
    }

    pub fn get_current_device_state(&self) -> DeviceState {
        DeviceState {
            connectivity_state: Some(self.updated_connectivity_state()),
            role: Some(self.get_current_role()),
            ..DeviceState::EMPTY
        }
    }

    pub fn is_initialized(&self) -> bool {
        // TODO: Evaluate, do we need this method?
        true
    }
}

impl<OT, NI> OtDriver<OT, NI>
where
    OT: Send + ot::InstanceInterface,
{
    /// Asynchronous task that waits for the given `DriverState`
    /// snapshot predicate closure to return true.
    ///
    /// If the predicate returns true, the task ends immediately.
    /// If the predicate returns false, the task will sleep until
    /// the next driver state change, upon which the predicate will
    /// be checked again.
    pub(crate) async fn wait_for_state<FN>(&self, predicate: FN)
    where
        FN: Fn(&DriverState<OT>) -> bool,
    {
        use std::ops::Deref;
        loop {
            {
                let driver_state = self.driver_state.lock();
                if predicate(driver_state.deref()) {
                    break;
                }
            }
            self.driver_state_change.wait().await;
        }
    }

    /// Called whenever the driver state has changed.
    pub(super) fn on_connectivity_state_change(
        &self,
        new_state: ConnectivityState,
        old_state: ConnectivityState,
    ) {
        fx_log_info!("State Change: {:?} -> {:?}", old_state, new_state);

        self.driver_state_change.trigger();

        match (old_state, new_state) {
            // TODO: Add state transition tasks here.

            // Unhandled state transition.
            (_, _) => {}
        }
    }

    pub fn update_connectivity_state(&self) {
        let mut driver_state = self.driver_state.lock();

        let new_connectivity_state = driver_state.updated_connectivity_state();

        if new_connectivity_state != driver_state.connectivity_state {
            let old_connectivity_state = driver_state.connectivity_state;
            driver_state.connectivity_state = new_connectivity_state;
            std::mem::drop(driver_state);
            self.driver_state_change.trigger();
            self.on_connectivity_state_change(new_connectivity_state, old_connectivity_state);
        }
    }

    pub(super) fn get_connectivity_state(&self) -> ConnectivityState {
        self.driver_state.lock().connectivity_state
    }
}
