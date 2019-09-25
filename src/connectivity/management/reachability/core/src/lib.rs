// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[macro_use]
extern crate log;
use fidl_fuchsia_net_stack as stack;
use fidl_fuchsia_netstack as netstack;
use network_manager_core::error;
use network_manager_core::hal;

/// `Monitor` monitors the reachability state.
pub struct Monitor {
    hal: hal::NetCfg,
}

impl Monitor {
    /// Create the monitoring service.
    pub fn new() -> Self {
        let hal = hal::NetCfg::new().unwrap(); // just crash on error for now.
        Monitor { hal }
    }

    /// Returns the underlying event streams associated with the open channels to fuchsia.net.stack
    /// and fuchsia.netstack.
    pub fn take_event_streams(
        &mut self,
    ) -> (stack::StackEventStream, netstack::NetstackEventStream) {
        self.hal.take_event_streams()
    }

    /// Processes an event coming from fuchsia.net.stack containing updates to
    /// properties associated with an interface. `OnInterfaceStatusChange` event is raised when an
    /// interface is enabled/disabled, connected/disconnected, or added/removed.
    pub async fn stack_event(&mut self, event: stack::StackEvent) -> error::Result<()> {
        match event {
            stack::StackEvent::OnInterfaceStatusChange { info } => {
                match self.hal.get_interface(info.id).await {
                    Some(iface) => {
                        info!("Stack: Interface change: {:#?} {:#?}", info, iface);
                        Ok(())
                    }
                    None => Ok(()),
                }
            }
        }
    }

    /// Processes an event coming from fuchsia.netstack containing updates to
    /// properties associated with an interface.
    pub async fn netstack_event(&mut self, event: netstack::NetstackEvent) -> error::Result<()> {
        match event {
            netstack::NetstackEvent::OnInterfacesChanged { interfaces } => {
                info!("NetStack: Interface change: {:#?} ", interfaces);
            }
        }
        Ok(())
    }

    /// `populate_state` queries the networks stack to determine current state.
    pub async fn populate_state(&mut self) -> error::Result<()> {
        for p in self.hal.ports().await?.iter() {
            info!("Existing ports: {:#?}", p)
        }
        for i in self.hal.interfaces().await?.into_iter() {
            info!("Existing interfaces: {:#?}", i)
        }
        Ok(())
    }
}
