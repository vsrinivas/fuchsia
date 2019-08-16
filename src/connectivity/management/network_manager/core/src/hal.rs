// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple port manager.

use crate::lifmgr;
use crate::{error, lifmgr::LifIpAddr};
use failure::{Error, ResultExt};
use fidl_fuchsia_net;
use fidl_fuchsia_net_stack::{StackMarker, StackProxy};
use fidl_fuchsia_netstack::{NetstackMarker, NetstackProxy};
use fuchsia_component::client::connect_to_service;
use std::collections::HashSet;
use std::convert::TryFrom;
use std::net::IpAddr;

// StackPortId is the port ID's used by the netstack.
// This is what is passed in the stack FIDL to denote the port or nic id.
#[derive(Eq, PartialEq, Hash, Debug, Copy, Clone)]
pub struct StackPortId(u64);
impl From<u64> for StackPortId {
    fn from(p: u64) -> StackPortId {
        StackPortId(p)
    }
}
impl From<PortId> for StackPortId {
    fn from(p: PortId) -> StackPortId {
        // TODO(dpradilla): This should be based on the mapping between physical location and
        // logical (from management plane point of view) port id.
        StackPortId::from(p.to_u64())
    }
}
impl StackPortId {
    // to_u64 converts it to u64, as some FIDL interfaces use the ID as a u64.
    pub fn to_u64(self) -> u64 {
        self.0
    }
    // to_u32 converts it to u32, as some FIDL interfaces use the ID as a u32.
    pub fn to_u32(self) -> u32 {
        match u32::try_from(self.0) {
            Ok(v) => v,
            e => {
                warn!("overflow converting StackPortId {:?}: {:?}", self.0, e);
                self.0 as u32
            }
        }
    }
}

#[derive(Eq, PartialEq, Hash, Debug, Copy, Clone)]
pub struct PortId(u64);
impl From<u64> for PortId {
    fn from(p: u64) -> PortId {
        PortId(p)
    }
}
impl From<StackPortId> for PortId {
    // TODO(dpradilla): This should be based on the mapping between physical location and
    // logical (from management plane point of view) port id.
    fn from(p: StackPortId) -> PortId {
        PortId(p.to_u64())
    }
}
impl PortId {
    // to_u64 converts it to u64, as some FIDL interfaces use the ID as a u64.
    pub fn to_u64(self) -> u64 {
        self.0
    }

    // to_u32 converts it to u32, as some FIDL interfaces use the ID as a u32.
    pub fn to_u32(self) -> u32 {
        match u32::try_from(self.0) {
            Ok(v) => v,
            e => {
                warn!("overflow converting StackPortId {:?}: {:?}", self.0, e);
                self.0 as u32
            }
        }
    }
}

pub struct NetCfg {
    stack: StackProxy,
    netstack: NetstackProxy,
    // id_in_use contains interface id's currently in use by the system
    id_in_use: HashSet<StackPortId>,
}

#[derive(Debug)]
pub struct Port {
    pub id: PortId,
    pub path: String,
}

#[derive(Debug)]
pub struct Interface {
    pub id: PortId,
    pub name: String,
    pub addr: Option<LifIpAddr>,
}

impl NetCfg {
    pub fn new() -> Result<Self, Error> {
        let stack = connect_to_service::<StackMarker>()
            .context("router_manager failed to connect to netstack")?;
        let netstack = connect_to_service::<NetstackMarker>()
            .context("router_manager failed to connect to netstack")?;
        Ok(NetCfg { stack, netstack, id_in_use: HashSet::new() })
    }
    // ports gets all physical ports in the system.
    pub async fn ports(&self) -> error::Result<Vec<Port>> {
        let ports = self.stack.list_interfaces().await.map_err(|_| error::Hal::OperationFailed)?;
        let p = ports
            .into_iter()
            .filter(|x| x.properties.topopath != "")
            .map(|x| Port { id: StackPortId::from(x.id).into(), path: x.properties.topopath })
            .collect::<Vec<Port>>();
        Ok(p)
    }

    /// interfaces returns all L3 interfaces with valid, non-local IPs in the system.
    pub async fn interfaces(&mut self) -> error::Result<Vec<Interface>> {
        let ifs = self
            .stack
            .list_interfaces()
            .await
            .map_err(|_| error::Hal::OperationFailed)?
            .iter()
            .filter_map(|i| {
                let id = StackPortId::from(i.id);
                self.id_in_use.insert(id);
                // Only return interfaces with an IPv4 address
                // TODO(dpradilla) support IPv6 and interfaces with multiple IPs? (is there a use
                // case given this context?)
                i.properties
                        .addresses
                        .iter()
                        .filter_map(|ip| {
                            match ip {
                                fidl_fuchsia_net_stack::InterfaceAddress {
                                    ip_address:
                                        fidl_fuchsia_net::IpAddress::Ipv4(
                                            fidl_fuchsia_net::Ipv4Address { addr },
                                        ),
                                    prefix_len,
                                } => {
                                    let ip_address = IpAddr::from(*addr);
                                    if ip_address.is_loopback()
                                        || ip_address.is_unspecified()
                                        || ip_address.is_multicast()
                                    {
                                        None
                                    } else {
                                        Some(Interface {
                                            id: id.into(),
                                            name: i.properties.topopath.clone(),
                                            addr: Some(LifIpAddr {
                                                address: IpAddr::from(*addr),
                                                prefix: *prefix_len,
                                            }),
                                        })
                                    }
                                }
                                // Only IPv4 for now.
                                _ => None,
                            }
                        })
                        .nth(0)
            })
            .collect();
        Ok(ifs)
    }
    pub async fn create_bridge(&mut self, ports: Vec<PortId>) -> error::Result<Interface> {
        let _br = self
            .netstack
            .bridge_interfaces(&mut ports.into_iter().map(|id| StackPortId::from(id).to_u32()))
            .await;
        // Find out what was the interface created, as there is no indication from above API.
        let ifs = self.stack.list_interfaces().await.map_err(|_| error::Hal::OperationFailed)?;
        if let Some(i) = ifs.iter().find(|x| self.id_in_use.insert(StackPortId::from(x.id))) {
            return Ok(Interface {
                id: StackPortId::from(i.id).into(),
                name: i.properties.topopath.clone(),
                addr: None,
            });
        }
        Err(error::RouterManager::HAL(error::Hal::BridgeNotFound))
    }

    /// delete_bridge deletes a bridge.
    pub async fn delete_bridge(&mut self, id: PortId) -> error::Result<()> {
        // TODO(dpradilla): what is the API for deleting a bridge? Call it
        info!("delete_bridge {:?} - Noop for now", id);
        Ok(())
    }

    /// set_ip_address configures an IP address.
    pub async fn set_ip_address<'a>(
        &'a mut self,
        pid: PortId,
        addr: &'a LifIpAddr,
    ) -> error::Result<()> {
        let r = self
            .stack
            .add_interface_address(
                StackPortId::from(pid).to_u64(),
                &mut addr.to_fidl_interface_address(),
            )
            .await;
        match r {
            Err(_) => Err(error::RouterManager::HAL(error::Hal::OperationFailed)),
            Ok(r) => match r {
                None => Ok(()),
                Some(e) => {
                    println!("could not set interface address: ${:?}", e);
                    Err(error::RouterManager::HAL(error::Hal::OperationFailed))
                }
            },
        }
    }

    /// unset_ip_address removes an IP address from the interface configuration.
    pub async fn unset_ip_address<'a>(
        &'a mut self,
        pid: PortId,
        addr: &'a LifIpAddr,
    ) -> error::Result<()> {
        let a = addr.to_fidl_address_and_prefix();
        // TODO(dpradilla): this needs to be changed to use the stack fidl once
        // this functionality is moved there. the u32 conversion won't be needed.
        let r = self
            .netstack
            .remove_interface_address(
                pid.to_u32(),
                &mut a.address.unwrap(),
                a.prefix_length.unwrap(),
            )
            .await;
        match r {
            Ok(fidl_fuchsia_netstack::NetErr {
                status: fidl_fuchsia_netstack::Status::Ok,
                message: _,
            }) => Ok(()),
            _ => Err(error::RouterManager::HAL(error::Hal::OperationFailed)),
        }
    }

    pub async fn set_interface_state(&mut self, pid: PortId, state: bool) -> error::Result<()> {
        let r = if state {
            self.stack.enable_interface(StackPortId::from(pid).to_u64())
        } else {
            self.stack.disable_interface(StackPortId::from(pid).to_u64())
        };
        match r.await {
            Ok(_) => Ok(()),
            _ => Err(error::RouterManager::HAL(error::Hal::OperationFailed)),
        }
    }

    pub async fn set_dhcp_client_state(&mut self, pid: PortId, enable: bool) -> error::Result<()> {
        let r = self.netstack.set_dhcp_client_status(StackPortId::from(pid).to_u32(), enable).await;
        match r {
            Ok(fidl_fuchsia_netstack::NetErr {
                status: fidl_fuchsia_netstack::Status::Ok,
                message: _,
            }) => Ok(()),
            _ => Err(error::RouterManager::HAL(error::Hal::OperationFailed)),
        }
    }

    // apply_manual_address updates the configured IP address.
    async fn apply_manual_ip<'a>(
        &'a mut self,
        pid: PortId,
        current: &'a Option<LifIpAddr>,
        desired: &'a Option<LifIpAddr>,
    ) -> error::Result<()> {
        match (current, desired) {
            (Some(current_ip), Some(desired_ip)) => {
                if current_ip != current_ip {
                    // There has been a change.
                    // Remove the old one and add the new one.
                    self.unset_ip_address(pid, &current_ip).await?;
                    self.set_ip_address(pid, &desired_ip).await?;
                }
            }
            (None, Some(desired_ip)) => {
                self.set_ip_address(pid, &desired_ip).await?;
            }
            (Some(current_ip), None) => {
                self.unset_ip_address(pid, &current_ip).await?;
            }
            // Nothing to do.
            (None, None) => {}
        };
        Ok(())
    }

    /// apply_properties applies the indicated LIF properties.
    pub async fn apply_properties<'a>(
        &'a mut self,
        pid: PortId,
        old: &'a lifmgr::LIFProperties,
        properties: &'a lifmgr::LIFProperties,
    ) -> error::Result<()> {
        match (old.dhcp, properties.dhcp) {
            // dhcp configuration transitions from enabled to disabled.
            (true, false) => {
                // Disable dhcp and apply manual address configuration.
                self.set_dhcp_client_state(pid, properties.dhcp).await?;
                self.apply_manual_ip(pid, &old.address, &properties.address).await?;
            }
            // dhcp configuration transitions from disabled to enabled.
            (false, true) => {
                // Remove any manual IP address and enable dhcp client.
                self.apply_manual_ip(pid, &old.address, &None).await?;
                self.set_dhcp_client_state(pid, properties.dhcp).await?;
            }
            // dhcp is still disabled, check for manual IP address changes.
            (false, false) => {
                self.apply_manual_ip(pid, &old.address, &properties.address).await?;
            }
            // No changes to dhcp configuration, it is still enabled, nothing to do.
            (true, true) => {}
        }
        if old.enabled != properties.enabled {
            info!("id {:?} updating state {:?}", pid, properties.enabled);
            self.set_interface_state(pid, properties.enabled).await?;
        }
        Ok(())
    }
}
