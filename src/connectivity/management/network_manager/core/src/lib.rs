// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

// In case we roll the toolchain and something we're using as a feature has been
// stabilized.
#![allow(stable_features)]
// TODO(dpradilla): reenable and add comments #![deny(missing_docs)]
#![deny(unreachable_patterns)]
#![deny(unused)]
#![deny(unused_imports)]

#[macro_use]
extern crate log;
pub mod error;
pub mod hal;
pub mod lifmgr;
pub mod packet_filter;
pub mod portmgr;
mod servicemgr;
use crate::lifmgr::{LIFProperties, LIFType, LifIpAddr};
use crate::portmgr::PortId;
use fidl_fuchsia_net_stack as stack;
use fidl_fuchsia_netstack as netstack;
use fidl_fuchsia_router_config::LifProperties;
use std::sync::atomic::{AtomicUsize, Ordering};

/// `DeviceState` holds the device state.
pub struct DeviceState {
    version: Version, // state version.
    port_manager: portmgr::PortManager,
    lif_manager: lifmgr::LIFManager,
    service_manager: servicemgr::Manager,
    packet_filter: packet_filter::PacketFilter,
    hal: hal::NetCfg,
}

impl DeviceState {
    //! Create an empty DeviceState.
    pub fn new(hal: hal::NetCfg, packet_filter: packet_filter::PacketFilter) -> Self {
        let v = 0;
        DeviceState {
            version: v,
            port_manager: portmgr::PortManager::new(),
            lif_manager: lifmgr::LIFManager::new(),
            service_manager: servicemgr::Manager::new(),
            packet_filter: packet_filter,
            hal,
        }
    }

    /// Returns the underlying event streams associated with the open channels to fuchsia.net.stack
    /// and fuchsia.netstack.
    pub fn take_event_streams(
        &mut self,
    ) -> (stack::StackEventStream, netstack::NetstackEventStream) {
        self.hal.take_event_streams()
    }

    /// Informs network manager of an external event from fuchsia.net.stack containing updates to
    /// properties associated with an interface. OnInterfaceStatusChange event is raised when an
    /// interface is enabled/disabled, connected/disconnected, or added/removed.
    pub async fn update_state_for_stack_event(
        &mut self,
        event: stack::StackEvent,
    ) -> error::Result<()> {
        match event {
            stack::StackEvent::OnInterfaceStatusChange { info } => {
                match self.hal.get_interface(info.id).await {
                    Some(iface) => self.notify_lif_added_or_updated(iface),
                    None => Ok(()),
                }
            }
        }
    }

    /// Informs network manager of an external event from fuchsia.netstack containing updates to
    /// properties associated with an interface.
    pub async fn update_state_for_netstack_event(
        &mut self,
        event: netstack::NetstackEvent,
    ) -> error::Result<()> {
        match event {
            netstack::NetstackEvent::OnInterfacesChanged { interfaces } => interfaces
                .into_iter()
                .try_for_each(|iface| self.notify_lif_added_or_updated(iface.into())),
        }
    }

    /// Update internal state with information about an LIF that was added or updated externally.
    fn notify_lif_added_or_updated(&mut self, iface: hal::Interface) -> error::Result<()> {
        match self.lif_manager.lif_at_port(iface.id) {
            Some(lif) => lif.update_with_address(iface.addr, iface.enabled)?,
            None => self.notify_lif_added(iface)?,
        };
        Ok(())
    }

    /// Update internal state with information about an LIF that was added externally.
    fn notify_lif_added(&mut self, iface: hal::Interface) -> error::Result<()> {
        if iface.addr.is_none() {
            return Ok(());
        }
        let port = iface.id;
        self.port_manager.use_port(&port);
        let l = lifmgr::LIF::new(
            self.version,
            LIFType::WAN,
            &iface.name,
            iface.id,
            vec![iface.id],
            0, // vlan
            Some(lifmgr::LIFProperties {
                address: iface.addr,
                enabled: true,
                ..Default::default()
            }),
        )
        .or_else(|e| {
            self.port_manager.release_port(&port);
            Err(e)
        })?;
        self.lif_manager.add_lif(&l).or_else(|e| {
            self.port_manager.release_port(&port);
            Err(e)
        })
    }

    /// `populate_state` populates the state based on lower layers state.
    pub async fn populate_state(&mut self) -> error::Result<()> {
        for p in self.hal.ports().await?.iter() {
            self.add_port(p.id, &p.path, self.version());
        }
        self.hal
            .interfaces()
            .await?
            .into_iter()
            .try_for_each(|iface| self.notify_lif_added(iface))?;
        self.version += 1;
        Ok(())
    }

    /// `add_port` adds a new port.
    pub fn add_port(&mut self, id: PortId, path: &str, v: Version) {
        self.port_manager.add_port(portmgr::Port::new(id, path, v));
        // This is a response to an event, not a configuration change. Version should not be
        // incremented.
    }

    // release_ports releases indicated ports.
    fn release_ports(&mut self, ports: &Vec<PortId>) {
        ports.iter().for_each(|p| {
            self.port_manager.release_port(p);
        });
    }

    /// `create_lif` creates a LIF of the indicated type.
    pub async fn create_lif(
        &mut self,
        lif_type: LIFType,
        name: String,
        vlan: u16,
        ports: Vec<PortId>,
    ) -> error::Result<lifmgr::LIF> {
        // Verify ports exist and can be used.
        let x = ports.iter().find(|p| !self.port_manager.use_port(p));
        if x.is_some() {
            self.release_ports(&ports);
            return Err(error::NetworkManager::LIF(error::Lif::InvalidPort));
        }
        let mut l = lifmgr::LIF::new(
            self.version,
            lif_type,
            &name,
            PortId::from(0),
            ports.clone(),
            vlan,
            Some(LIFProperties::default()),
        )
        .or_else(|e| {
            self.release_ports(&ports);
            Err(e)
        })?;
        if ports.len() > 1 {
            // Multiple ports, bridge them.
            let i = self.hal.create_bridge(ports.clone()).await.or_else(|e| {
                self.release_ports(&ports);
                Err(e)
            })?;
            // LIF ID is associated with the bridge.
            l.set_pid(i.id);
        } else if ports.len() == 1 {
            l.set_pid(ports[0]);
        }
        let r = self.lif_manager.add_lif(&l);
        if r.is_err() {
            self.release_ports(&ports);
            // nothing to do if delete_bridge fails, all state changes have been reverted
            // already, just return an error to let caller handle it as appropriate.
            self.hal.delete_bridge(l.pid()).await?;
            return Err(r.unwrap_err());
        }
        // all went well, increase version.
        self.version += 1;
        Ok(l)
    }

    /// `delete_lif` creates a LIF of the indicated type.
    pub async fn delete_lif(&mut self, lif_id: UUID) -> Result<(), error::NetworkManager> {
        // Locate the lif to delete.
        let lif = self.lif_manager.lif_mut(&lif_id);
        let lif = match lif {
            None => {
                info!("delete_lif: lif not found {:?} ", lif_id);
                return Err(error::NetworkManager::LIF(error::Lif::NotFound));
            }
            Some(x) => x.clone(),
        };
        // reset all properties, shut down LIF
        self.hal
            .apply_properties(
                lif.pid(),
                lif.properties(),
                &LIFProperties { dhcp: false, address: None, enabled: false },
            )
            .await?;
        // delete bridge if there is one and shut down the related ports
        let ports = lif.ports();
        if ports.len() > 1 {
            self.hal.delete_bridge(lif.pid()).await?;
            //TODO(dpradilla) shut down the ports.
        }
        self.release_ports(&ports.collect());
        // delete from database
        if self.lif_manager.remove_lif(lif_id).is_none() {
            return Err(error::NetworkManager::LIF(error::Lif::NotFound));
        }
        Ok(())
    }

    /// `update_lif_properties` configures an interface as indicated and updates the LIF information.
    pub async fn update_lif_properties(
        &mut self,
        lif_id: UUID,
        properties: LifProperties,
    ) -> error::Result<()> {
        let l = self.lif_manager.lif_mut(&lif_id);
        let lif = match l {
            None => {
                info!("update_lif_properties: lif not found {:?} ", lif_id);
                return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
            }
            Some(x) => x,
        };

        info!("update_lif_properties: setting properties {:?}", properties);
        match properties {
            // Get existing configuration
            // Merge with new one
            // Validate updated config
            // Apply differences
            // If failure, revert back
            // Report result.
            LifProperties::Wan(p) => {
                let old = lif.properties();
                let mut lp = LIFProperties::default();

                match &p.connection_type {
                    None => {}
                    Some(fidl_fuchsia_router_config::WanConnection::Direct) => {
                        info!("connection_type DIRECT ");
                    }
                    Some(cfg) => {
                        info!("connection_type {:?}", cfg);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.connection_parameters {
                    None => {}
                    Some(cfg) => {
                        info!("connection parameters {:?}", cfg);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.hostname {
                    None => {}
                    Some(cfg) => {
                        info!("hostname {:?}", cfg);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.clone_mac {
                    None => {}
                    Some(cfg) => {
                        info!("clone mac {:?}", cfg);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.mtu {
                    None => {}
                    Some(cfg) => {
                        info!("mtu  {:?}", cfg);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.metric {
                    None => {}
                    Some(cfg) => {
                        info!("metric {:?}", cfg);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.address_method {
                    Some(fidl_fuchsia_router_config::WanAddressMethod::Automatic) => {
                        lp.dhcp = true;
                        lp.address = None;
                    }
                    Some(fidl_fuchsia_router_config::WanAddressMethod::Manual) => {
                        lp.dhcp = false;
                    }
                    None => {}
                };
                match &p.address_v4 {
                    None => {}
                    Some(fidl_fuchsia_router_config::CidrAddress {
                        address: Some(address),
                        prefix_length: Some(prefix_length),
                    }) => {
                        // The WAN IPv4 address is changing.
                        if lp.dhcp {
                            warn!(
                                "Setting a static ip is not allowed when \
                                 a dhcp client is configured"
                            );
                            return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                        }
                        info!("setting ip to {:?} {:?}", address, prefix_length);
                        let v4addr = LifIpAddr::from(p.address_v4.as_ref().unwrap());
                        self.service_manager.set_global_ip_nat(v4addr.clone(), lif.pid());
                        lp.address = Some(v4addr);
                    }
                    _ => {
                        warn!("invalid address {:?}", p.address_v4);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.gateway_v4 {
                    None => {}
                    Some(gw) => {
                        info!("setting gateway {:?}", gw);
                        //  TODO(dpradilla): implement. - verify gw is in local network
                    }
                }
                match &p.connection_v6_mode {
                    None => {}
                    Some(cfg) => {
                        info!("v6 mode {:?}", cfg);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.address_v6 {
                    None => {}
                    Some(fidl_fuchsia_router_config::CidrAddress {
                        address: Some(address),
                        prefix_length: Some(prefix_length),
                    }) => {
                        // WAN IPv6 address is changing.
                        if lp.dhcp {
                            warn!(
                                "Setting a static ip is not allowed when \
                                 a dhcp client is configured"
                            );
                            return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                        }
                        info!("setting ip to {:?} {:?}", address, prefix_length);
                        let a = LifIpAddr::from(p.address_v6.as_ref().unwrap());
                        lp.address = Some(a);
                    }
                    _ => {
                        warn!("invalid address {:?}", p.address_v6);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.gateway_v6 {
                    None => {}
                    Some(gw) => {
                        info!("setting gateway {:?}", gw);
                        //  TODO(dpradilla): implement. - verify gw is in local network
                    }
                };
                self.hal.apply_properties(lif.pid(), &old, &lp).await?;
                lif.set_properties(self.version, lp)?;
                self.version += 1;
                Ok(())
            }
            LifProperties::Lan(p) => {
                let old = lif.properties();
                let mut lp = LIFProperties::default();

                match p.enable_dhcp_server {
                    None => {}
                    Some(true) => {
                        info!("enable DHCP server");
                        self.service_manager.enable_server(&lif)?;
                    }
                    Some(false) => {
                        info!("disable DHCP server");
                        self.service_manager.disable_server(&lif);
                    }
                };
                match &p.dhcp_config {
                    None => {}
                    Some(cfg) => {
                        info!("DHCP server configuration {:?}", cfg);
                    }
                };
                match &p.address_v4 {
                    None => {}
                    Some(fidl_fuchsia_router_config::CidrAddress {
                        address: Some(address),
                        prefix_length: Some(prefix_length),
                    }) => {
                        // LAN IPv4 address is changing.
                        info!("setting ip to {:?} {:?}", address, prefix_length);
                        let v4addr = LifIpAddr::from(p.address_v4.as_ref().unwrap());
                        self.service_manager.set_local_subnet_nat(v4addr.clone());
                        lp.address = Some(v4addr);
                    }
                    _ => {
                        warn!("invalid address {:?}", p.address_v4);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.address_v6 {
                    None => {}
                    Some(fidl_fuchsia_router_config::CidrAddress {
                        address: Some(address),
                        prefix_length: Some(prefix_length),
                    }) => {
                        // LAN IPv6 address is changing.
                        info!("setting ip to {:?} {:?}", address, prefix_length);
                        let a = LifIpAddr::from(p.address_v6.as_ref().unwrap());
                        lp.address = Some(a);
                    }
                    _ => {
                        warn!("invalid address {:?}", p.address_v6);
                        return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
                    }
                };
                match &p.enable {
                    None => {}
                    Some(enable) => {
                        info!("enable {:?}", enable);
                        lp.enabled = *enable
                    }
                };

                self.hal.apply_properties(lif.pid(), &old, &lp).await?;
                lif.set_properties(self.version, lp)?;
                if self.service_manager.is_nat_enabled() {
                    if let Err(e) =
                        self.packet_filter.update_nat(self.service_manager.get_nat_config()).await
                    {
                        warn!("Failed to enable nat: {:?}", e);
                        return Err(error::NetworkManager::SERVICE(
                            error::Service::ErrorFailedEnableNat,
                        ));
                    }
                }
                self.version += 1;
                Ok(())
            }
        }
    }

    /// `lif` returns the LIF with the given uuid.
    pub fn lif(&self, lif_id: UUID) -> Option<&lifmgr::LIF> {
        self.lif_manager.lif(&lif_id)
    }
    /// `lifs` returns all the LIFs of the given type.
    pub fn lifs(&self, t: LIFType) -> impl Iterator<Item = &lifmgr::LIF> {
        self.lif_manager.lifs(t)
    }
    /// `ports` returns all ports managed by configuration manager.
    pub fn ports(&self) -> impl ExactSizeIterator<Item = &portmgr::Port> {
        self.port_manager.ports()
    }

    /// `version` returns the current configuration version.
    /// The configuration version is monotonically increased every time there is a configuration
    /// change.
    pub fn version(&self) -> Version {
        self.version
    }

    /// `set_filter` installs a new packet filter rule.
    pub async fn set_filter(
        &self,
        rule: fidl_fuchsia_router_config::FilterRule,
    ) -> error::Result<()> {
        match self.packet_filter.set_filter(rule).await {
            Ok(_) => Ok(()),
            Err(e) => {
                warn!("Failed to set new filter rules: {:?}", e);
                Err(error::NetworkManager::SERVICE(error::Service::ErrorAddingPacketFilterRules))
            }
        }
    }

    /// `get_filters` returns the currently installed packet filter rules.
    pub async fn get_filters(&self) -> error::Result<Vec<fidl_fuchsia_router_config::FilterRule>> {
        self.packet_filter.get_filters().await.map_err(|e| {
            warn!("Failed to get filter rules: {:?}", e);
            error::NetworkManager::SERVICE(error::Service::ErrorGettingPacketFilterRules)
        })
    }
}

type Version = u64;
type UUID = u128;

static ID: AtomicUsize = AtomicUsize::new(1);

fn generate_uuid() -> UUID {
    ID.fetch_add(1, Ordering::Relaxed) as UUID
}

#[derive(Eq, PartialEq, Debug, Copy, Clone)]
pub struct ElementId {
    uuid: UUID,
    version: Version,
}
impl ElementId {
    pub fn new(v: Version) -> Self {
        ElementId { uuid: generate_uuid(), version: v }
    }
    pub fn update(mut self, version: Version) {
        self.version = version;
    }
    pub fn uuid(&self) -> UUID {
        self.uuid.clone()
    }
    pub fn version(&self) -> u64 {
        self.version
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_net as net;
    use fuchsia_async as fasync;
    use std::net::IpAddr;

    #[fasync::run_singlethreaded(test)]
    async fn test_new_device_state() {
        let d = DeviceState::new(
            hal::NetCfg::new().unwrap(),
            packet_filter::PacketFilter::start().unwrap(),
        );

        assert_eq!(d.version, 0);
        assert_eq!(d.port_manager.ports().count(), 0);
        assert_eq!(d.lif_manager.all_lifs().count(), 0);
    }

    fn net_interface(port: u32, addr: [u8; 4]) -> netstack::NetInterface {
        netstack::NetInterface {
            id: port,
            flags: netstack::NET_INTERFACE_FLAG_UP | netstack::NET_INTERFACE_FLAG_DHCP,
            features: 0,
            configuration: 0,
            name: port.to_string(),
            addr: net::IpAddress::Ipv4(net::Ipv4Address { addr }),
            netmask: net::IpAddress::Ipv4(net::Ipv4Address { addr: [255, 255, 255, 0] }),
            broadaddr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 255] }),
            ipv6addrs: vec![],
            hwaddr: [1, 2, 3, 4, 5, port as u8].to_vec(),
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_update_state_for_netstack_event() {
        let mut device_state = DeviceState::new(
            hal::NetCfg::new().unwrap(),
            packet_filter::PacketFilter::start().unwrap(),
        );

        // Create a few ports.
        device_state.port_manager.add_port(portmgr::Port {
            e_id: ElementId::new(1),
            port_id: hal::PortId::from(5),
            path: "path".to_string(),
        });
        device_state.port_manager.add_port(portmgr::Port {
            e_id: ElementId::new(2),
            port_id: hal::PortId::from(4),
            path: "path".to_string(),
        });
        let ports = device_state.ports().collect::<Vec<&portmgr::Port>>();
        assert_eq!(ports.len(), 2);

        // Netstack informs network manager of a new interface at port 5 with ip 1.2.3.4.
        assert!(device_state
            .update_state_for_netstack_event(netstack::NetstackEvent::OnInterfacesChanged {
                interfaces: vec![net_interface(5, [1, 2, 3, 4])],
            },)
            .await
            .is_ok());
        // Port 5 should now be marked used.
        assert!(!device_state.port_manager.port_available(&hal::PortId::from(5)).unwrap());
        // Assert that network manager now knows about this new interface.
        {
            let lifs: Vec<&lifmgr::LIF> = device_state.lifs(LIFType::WAN).collect();
            assert_eq!(lifs.len(), 1);
            assert_eq!(lifs[0].pid().to_u64(), 5);
            assert_eq!(
                lifs[0].properties().address.as_ref().unwrap().address,
                IpAddr::from([1, 2, 3, 4])
            );
        }

        // Netstack informs network manager of update to interface at port 5 and a new interface
        // at port 4.
        assert!(device_state
            .update_state_for_netstack_event(netstack::NetstackEvent::OnInterfacesChanged {
                interfaces: vec![net_interface(5, [2, 3, 4, 5]), net_interface(4, [3, 4, 5, 6])],
            },)
            .await
            .is_ok());
        // Assert that network manager now knows about the new interface and updates to the other
        // interface.
        {
            let lifs: Vec<&lifmgr::LIF> = device_state.lifs(LIFType::WAN).collect();
            assert_eq!(lifs.len(), 2);
            let lif5 = lifs.iter().find(|lif| lif.pid().to_u64() == 5).unwrap();
            assert_eq!(
                lif5.properties().address.as_ref().unwrap().address,
                IpAddr::from([2, 3, 4, 5])
            );
            let lif4 = lifs.iter().find(|lif| lif.pid().to_u64() == 4).unwrap();
            assert_eq!(
                lif4.properties().address.as_ref().unwrap().address,
                IpAddr::from([3, 4, 5, 6])
            );
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_dont_update_state_for_netstack_event_without_ip() {
        let mut device_state = DeviceState::new(
            hal::NetCfg::new().unwrap(),
            packet_filter::PacketFilter::start().unwrap(),
        );
        device_state.port_manager.add_port(portmgr::Port {
            e_id: ElementId::new(1),
            port_id: hal::PortId::from(5),
            path: "path".to_string(),
        });
        assert!(device_state
            .update_state_for_netstack_event(netstack::NetstackEvent::OnInterfacesChanged {
                interfaces: vec![net_interface(5, [0, 0, 0, 0])],
            },)
            .await
            .is_ok());
        // Port should still be free.
        assert!(device_state.port_manager.port_available(&hal::PortId::from(5)).unwrap());
        assert_eq!(device_state.lifs(LIFType::WAN).next(), None);
    }
}
