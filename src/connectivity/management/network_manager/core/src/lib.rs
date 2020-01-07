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
pub mod config;
pub mod error;
pub mod hal;
pub(crate) mod interface;
pub mod lifmgr;
pub mod oir;
pub mod packet_filter;
pub mod portmgr;
mod servicemgr;

use crate::lifmgr::{LIFProperties, LIFType, LifIpAddr};
use crate::portmgr::PortId;
use fidl_fuchsia_net_stack as stack;
use fidl_fuchsia_netstack as netstack;
use fidl_fuchsia_router_config::LifProperties;
use std::sync::atomic::{AtomicUsize, Ordering};

// TODO(cgibson): Remove this when the config api supports LAN interface configuration.
const NUMBER_OF_PORTS_IN_LAN: usize = 3;

/// `DeviceState` holds the device state.
pub struct DeviceState {
    version: Version, // state version.
    port_manager: portmgr::PortManager,
    lif_manager: lifmgr::LIFManager,
    service_manager: servicemgr::Manager,
    packet_filter: packet_filter::PacketFilter,
    dns_config: DnsConfig,
    hal: hal::NetCfg,
    config: config::Config,
    lans: Vec<PortId>,
}

#[derive(Debug, Clone, Copy)]
pub enum DnsPolicy {
    /// Can not be replaced by dynamically learned DNS configuration,
    /// will overwrite existing configuration.
    Static,
    /// Can be replaced by dynamically learned DNS configuration,
    /// will not overwrite existing configuration.
    Replaceable,
    /// Will merge with existing configuration.
    Merge,
}

impl From<DnsPolicy> for fidl_fuchsia_router_config::DnsPolicy {
    fn from(p: DnsPolicy) -> Self {
        match p {
            DnsPolicy::Static => fidl_fuchsia_router_config::DnsPolicy::Static,
            DnsPolicy::Replaceable => fidl_fuchsia_router_config::DnsPolicy::Replaceable,
            DnsPolicy::Merge => fidl_fuchsia_router_config::DnsPolicy::Merge,
        }
    }
}

impl Default for DnsPolicy {
    fn default() -> Self {
        DnsPolicy::Static
    }
}

impl From<fidl_fuchsia_router_config::DnsPolicy> for DnsPolicy {
    fn from(p: fidl_fuchsia_router_config::DnsPolicy) -> Self {
        match p {
            fidl_fuchsia_router_config::DnsPolicy::Static => DnsPolicy::Static,
            fidl_fuchsia_router_config::DnsPolicy::Replaceable => DnsPolicy::Replaceable,
            fidl_fuchsia_router_config::DnsPolicy::Merge => DnsPolicy::Merge,
            _ => DnsPolicy::default(),
        }
    }
}

#[derive(Debug)]
struct DnsConfig {
    id: ElementId,
    servers: Vec<fidl_fuchsia_net::IpAddress>,
    domain: Option<String>,
    policy: DnsPolicy,
}

impl From<&DnsConfig> for fidl_fuchsia_router_config::DnsResolverConfig {
    fn from(c: &DnsConfig) -> Self {
        fidl_fuchsia_router_config::DnsResolverConfig {
            element: c.id.into(),
            policy: c.policy.into(),
            search: fidl_fuchsia_router_config::DnsSearch {
                domain_name: c.domain.clone(),
                servers: c.servers.clone(),
            },
        }
    }
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
            packet_filter,
            hal,
            lans: Vec::new(),
            dns_config: DnsConfig {
                id: ElementId::new(v),
                servers: Default::default(),
                domain: Default::default(),
                policy: Default::default(),
            },
            config: config::Config::new(
                "/data/user_config.json",
                "/pkg/data/factory_config.json",
                "/pkg/data/device_schema.json",
            ),
        }
    }

    /// Loads the device configuration.
    pub async fn load_config(&mut self) -> error::Result<()> {
        if let Err(e) = self.config.load_config().await {
            error!("Failed to validate config from both user and factory files: {}", e);
            Err(e)
        } else {
            info!("Successfully loaded configuration!");
            Ok(())
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

    /// Restores state of global system options and services from stored configuration.
    pub async fn setup_services(&self) -> error::Result<()> {
        info!("Restoring system services state...");
        self.hal.set_ip_forwarding(self.config.get_ip_forwarding_state()).await?;
        // TODO(cgibson): Configure DHCP server.
        // TODO(cgibson): Configure packet filters.
        Ok(())
    }

    /// Configures a WAN uplink from stored configuration.
    async fn configure_wan(&mut self, pid: PortId, topological_path: &str) -> error::Result<()> {
        let wan_name = self.config.get_wan_interface_name(topological_path)?;
        let properties = self.config.create_wan_properties(topological_path)?;
        let lif = self.create_lif(LIFType::WAN, wan_name, None, &[pid]).await?;
        self.update_lif_properties(lif.id().uuid(), &properties.to_fidl_wan()).await?;
        info!("WAN configured: pid: {:?}, lif: {:?}, properties: {:?} ", pid, lif, properties);
        Ok(())
    }

    async fn configure_lan(&mut self, pids: &[PortId]) -> error::Result<()> {
        let lif = self.create_lif(LIFType::LAN, "lan".to_string(), Some(2), pids).await?;
        let properties = crate::lifmgr::LIFProperties {
            enabled: true,
            dhcp: false,
            address: Some(LifIpAddr { address: "192.168.51.1".parse().unwrap(), prefix: 24 }),
        }
        .to_fidl_wan();
        info!("LAN configured: lif: {:?} iproperties: {:?} ", lif, properties);
        self.update_lif_properties(lif.id().uuid(), &properties).await?;
        for pid in pids {
            self.hal.set_interface_state(*pid, true).await?
        }
        Ok(())
    }

    async fn apply_configuration_on_new_port(
        &mut self,
        pid: PortId,
        topological_path: &str,
    ) -> error::Result<()> {
        if self.config.device_id_is_a_wan_uplink(topological_path) {
            info!("Discovered a new uplink: {}", topological_path);
            return self.configure_wan(pid, topological_path).await;
        }
        // TODO(dpradilla) Remove this temporaty code once there
        // is a way to add to an existing bridge
        // This code is just for testing purposes,
        // until support for adding/removing ports from a
        // bridge is added to netstack.
        self.lans.push(pid);
        if self.lans.len() == NUMBER_OF_PORTS_IN_LAN {
            let lans = self.lans.clone();
            info!("LAN ports : {:?}", lans);
            return self.configure_lan(&lans).await;
        }
        self.hal.set_ip_forwarding(true).await?;
        Ok(())
    }

    pub async fn port_with_topopath(&self, path: &str) -> error::Result<Option<hal::Port>> {
        let port = self.hal.ports().await?.into_iter().find(|x| x.path == path);
        info!("ports {:?}", port);
        Ok(port)
    }

    /// Informs network manager of an external event from fuchsia.netstack containing updates to
    /// properties associated with an interface.
    pub async fn oir_event(&mut self, event: oir::OIRInfo) -> error::Result<()> {
        match event.action {
            oir::Action::ADD => {
                // Add to netstack if not there already.
                let pid = if let Some(p) = self.port_with_topopath(&event.topological_path).await? {
                    info!("port already added {}: {:?}", &event.topological_path, p);
                    p.id
                } else {
                    let info = event.device_information.ok_or(error::Oir::MissingInformation)?;
                    let port =
                        oir::PortDevice::new(&event.file_path, &event.topological_path, info)?;
                    info!("adding port {}: {:?}", &event.topological_path, port);
                    self.hal.add_ethernet_device(event.device_channel.unwrap(), port).await?
                };

                // Verify port manager is consistent.
                if let Some(id) = self.port_manager.port_id(&event.topological_path) {
                    if pid != *id {
                        error!("port {:?} already exists with id {:?}", event.topological_path, id);
                        return Err(error::NetworkManager::OIR(error::Oir::InconsistentState));
                    }
                    return Ok(());
                }

                // Port manager does not know about this port, add it.

                // No version change, this is just added a port insertion,
                // not a configuration update.
                self.add_port(pid, &event.topological_path, self.version());

                // Apply initial configuration.
                self.apply_configuration_on_new_port(pid, &event.topological_path).await?
            }
            oir::Action::REMOVE => {
                // Tell portmanager interface is removed. It should keep any config there.
                // Tell LIF manager associated port is gone, keep config there so it can be
                // reapplied if it comes back.
                info!("device {:?} will be removed", event.topological_path);
                // Tell netstack interface is gone.
                oir::remove_interface(&event.topological_path);
            }
        }
        Ok(())
    }

    /// Update internal state with information about an LIF that was added or updated externally.
    /// When an interface not known before is found, it will add it.
    /// If it's a know interface, it will log the new operational state.
    fn notify_lif_added_or_updated(&mut self, iface: hal::Interface) -> error::Result<()> {
        match self.lif_manager.lif_at_port(iface.id) {
            Some(lif) => log_property_change(lif, iface),
            None => self.notify_lif_added(iface)?,
        };
        Ok(())
    }

    /// Update internal state with information about an LIF that was added externally.
    fn notify_lif_added(&mut self, iface: hal::Interface) -> error::Result<()> {
        if iface.get_address().is_none() {
            return Ok(());
        }
        let port = iface.id;
        self.port_manager.use_port(port);
        let l = lifmgr::LIF::new(
            self.version,
            LIFType::WAN,
            &iface.name,
            iface.id,
            vec![iface.id],
            0, // vlan
            Some(lifmgr::LIFProperties {
                dhcp: iface.dhcp_client_enabled,
                address: iface.get_address(),
                enabled: true,
            }),
        )
        .or_else(|e| {
            self.port_manager.release_port(port);
            Err(e)
        })?;
        self.lif_manager.add_lif(&l).or_else(|e| {
            self.port_manager.release_port(port);
            Err(e)
        })?;
        // TODO(guzt): This should ideally compare metrics or be manually settable when there are
        // multiple WAN ports.
        if let Some(addr) = iface.get_address() {
            self.service_manager.set_global_ip_nat(addr, port)
        }
        Ok(())
    }

    /// Populate state based on lower layers.
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

    /// Adds a new port.
    pub fn add_port(&mut self, id: PortId, path: &str, v: Version) {
        self.port_manager.add_port(portmgr::Port::new(id, path, v));
        // This is a response to an event, not a configuration change. Version should not be
        // incremented.
    }

    /// Releases the given ports.
    fn release_ports(&mut self, ports: &[PortId]) {
        ports.iter().for_each(|p| {
            self.port_manager.release_port(*p);
        });
    }

    /// Creates a LIF of the given type.
    pub async fn create_lif(
        &mut self,
        lif_type: LIFType,
        name: String,
        vlan: Option<u16>,
        ports: &[PortId],
    ) -> error::Result<lifmgr::LIF> {
        if ports.is_empty() {
            // At least one port is needed.
            return Err(error::NetworkManager::LIF(error::Lif::NotSupported));
        }
        let reserved: Vec<PortId> = ports
            .iter()
            .filter_map(|p| if self.port_manager.use_port(*p) { Some(*p) } else { None })
            .collect();
        if reserved.len() != ports.len() {
            self.release_ports(&reserved);
            return Err(error::NetworkManager::LIF(error::Lif::InvalidPort));
        }

        // TODO(cgibson): Once we can configure VLANs, we need to handle VLAN ID 0 vs None, since
        // VLAN ID 0x0000 is a reserved value in dot1q. fxb/41746.
        let vid = match vlan {
            Some(v) => v,
            None => 0,
        };

        let mut l = lifmgr::LIF::new(
            self.version,
            lif_type,
            &name,
            PortId::from(0),
            reserved.clone(),
            vid,
            Some(LIFProperties::default()),
        )
        .or_else(|e| {
            self.release_ports(&reserved);
            Err(e)
        })?;
        if reserved.len() > 1 {
            // Multiple ports, bridge them.
            let i = self.hal.create_bridge(reserved.clone()).await.or_else(|e| {
                self.release_ports(&reserved);
                Err(e)
            })?;
            // LIF ID is associated with the bridge.
            l.set_pid(i.id);
        } else {
            l.set_pid(reserved[0]);
        }
        let r = self.lif_manager.add_lif(&l);
        if let Err(e) = r {
            self.release_ports(&reserved);
            // nothing to do if delete_bridge fails, all state changes have been reverted
            // already, just return an error to let caller handle it as appropriate.
            self.hal.delete_bridge(l.pid()).await?;
            return Err(e);
        }
        // all went well, increase version.
        self.version += 1;
        Ok(l)
    }

    /// Deletes the LIF of the given type.
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
        self.release_ports(&ports.collect::<Vec<PortId>>());
        // delete from database
        if self.lif_manager.remove_lif(lif_id).is_none() {
            return Err(error::NetworkManager::LIF(error::Lif::NotFound));
        }
        Ok(())
    }

    /// Configures an interface with the given properties, and updates the LIF information.
    pub async fn update_lif_properties(
        &mut self,
        lif_id: UUID,
        properties: &LifProperties,
    ) -> error::Result<()> {
        let lif = match self.lif_manager.lif_mut(&lif_id) {
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
                let mut lp = old.clone();

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
                        info!("Setting WAN IPv4 address to {:?}/{:?}", address, prefix_length);
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
                    Some(fidl_fuchsia_router_config::WanIpV6ConnectionMode::Passthrough) => {
                        info!("v6 mode Passthrough");
                    }
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
                        info!("Setting WAN IPv6 to {:?}/{:?}", address, prefix_length);
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
                if let Some(enable) = &p.enable {
                    info!("enable {:?}", enable);
                    lp.enabled = *enable
                };
                self.hal.apply_properties(lif.pid(), &old, &lp).await?;
                lif.set_properties(self.version, lp)?;
                self.version += 1;
                Ok(())
            }
            LifProperties::Lan(p) => {
                let old = lif.properties();
                let mut lp = old.clone();

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
                        info!("Setting LAN IPv4 address to {:?}/{:?}", address, prefix_length);
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
                        info!("Setting LAN IPv6 address to {:?}/{:?}", address, prefix_length);
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
                if let Err(e) = self.hal.apply_properties(lif.pid(), &old, &lp).await {
                    // TODO(cgibson): Need to roll back any partially applied configuration here.
                    warn!("Failed to update HAL properties: {:?}; Version not incremented.", e);
                    return Err(e);
                }
                if let Err(e) = lif.set_properties(self.version, lp) {
                    // TODO(cgibson): Need to roll back any partially applied configuration here.
                    warn!("Failed to update LIF properties: {:?}; Version not incremented.", e);
                    return Err(e);
                }
                match self.update_nat_config().await {
                    // If the result of this call was `UpdateNatPendingConfig` or `NatNotEnabled`,
                    // then NAT is not yet ready to be enabled until we have more configuration
                    // data.
                    Ok(_)
                    | Err(error::NetworkManager::SERVICE(error::Service::UpdateNatPendingConfig))
                    | Err(error::NetworkManager::SERVICE(error::Service::NatNotEnabled)) => {}
                    Err(e) => {
                        // Otherwise, this was an actual error and we should not increment the
                        // version number.
                        error!("Failed to update NAT rules: {:?}; Version not incremented.", e);
                        return Err(e);
                    }
                }
                self.version += 1;
                Ok(())
            }
        }
    }

    /// Returns the LIF with the given `UUID`.
    pub fn lif(&self, lif_id: UUID) -> Option<&lifmgr::LIF> {
        self.lif_manager.lif(&lif_id)
    }

    /// Returns all LIFs of the given type.
    pub fn lifs(&self, t: LIFType) -> impl Iterator<Item = &lifmgr::LIF> {
        self.lif_manager.lifs(t)
    }
    /// Returns all managed ports.
    pub fn ports(&self) -> impl ExactSizeIterator<Item = &portmgr::Port> {
        self.port_manager.ports()
    }

    /// Returns the current configuration version.
    ///
    /// The configuration version is monotonically increased every time there is a configuration
    /// change.
    pub fn version(&self) -> Version {
        self.version
    }

    /// Updates the current NAT configuration.
    pub async fn update_nat_config(&mut self) -> error::Result<()> {
        self.packet_filter.update_nat_config(self.service_manager.get_nat_config()).await
    }

    /// Enables NAT.
    pub async fn enable_nat(&mut self) -> error::Result<()> {
        if self.service_manager.is_nat_enabled() {
            return Err(error::NetworkManager::SERVICE(error::Service::NatAlreadyEnabled));
        }
        if let Err(e) = self.hal.set_ip_forwarding(true).await {
            warn!("Failed to enable IP forwarding: {:?}", e);
            return Err(error::NetworkManager::SERVICE(
                error::Service::ErrorEnableIpForwardingFailed,
            ));
        }
        match self.packet_filter.update_nat_config(self.service_manager.get_nat_config()).await {
            Err(error::NetworkManager::SERVICE(error::Service::NatNotEnabled)) => {
                self.service_manager.enable_nat();
                self.packet_filter.update_nat_config(self.service_manager.get_nat_config()).await
            }
            Err(e) => Err(e),
            Ok(_) => Ok(()),
        }
    }

    /// Disables NAT.
    pub async fn disable_nat(&mut self) -> error::Result<()> {
        if !self.service_manager.is_nat_enabled() {
            return Err(error::NetworkManager::SERVICE(error::Service::NatNotEnabled));
        }
        if let Err(e) = self.hal.set_ip_forwarding(false).await {
            warn!("Failed to disable IP forwarding: {:?}", e);
            return Err(error::NetworkManager::SERVICE(
                error::Service::ErrorDisableIpForwardingFailed,
            ));
        }
        self.packet_filter.clear_nat_rules().await?;
        self.service_manager.disable_nat();
        Ok(())
    }

    /// Returns whether NAT is enabled or not.
    pub fn is_nat_enabled(&self) -> bool {
        self.service_manager.is_nat_enabled()
    }

    /// Installs a new packet filter rule.
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

    /// Returns the currently installed packet filter rules.
    pub async fn get_filters(&self) -> error::Result<Vec<fidl_fuchsia_router_config::FilterRule>> {
        self.packet_filter.get_filters().await.map_err(|e| {
            warn!("Failed to get filter rules: {:?}", e);
            error::NetworkManager::SERVICE(error::Service::ErrorGettingPacketFilterRules)
        })
    }

    pub async fn set_dns_resolver(
        &mut self,
        servers: &mut [fidl_fuchsia_net::IpAddress],
        domain: Option<String>,
        policy: fidl_fuchsia_router_config::DnsPolicy,
    ) -> error::Result<ElementId> {
        if domain.is_some() {
            //TODO(dpradilla): lift this restriction.
            warn!("setting the dns search domain name is not supported {:?}", domain);
            return Err(error::NetworkManager::SERVICE(error::Service::NotSupported));
        }
        let p = if policy == fidl_fuchsia_router_config::DnsPolicy::NotSet {
            DnsPolicy::default()
        } else {
            DnsPolicy::from(policy)
        };
        // Keeping dns_config sorted.
        servers.sort();
        if servers.len() == self.dns_config.servers.len()
            && servers.iter().zip(&self.dns_config.servers).all(|(a, b)| a == b)
        {
            // No change needed.
            return Ok(self.dns_config.id);
        }
        // Just return the error, nothing to undo.
        self.hal.set_dns_resolver(servers, domain.clone(), p).await?;
        self.dns_config.id.version = self.version;
        self.dns_config.servers = servers.to_vec();
        self.version += 1;
        Ok(self.dns_config.id)
    }

    /// Returns the dns resolver configuration.
    pub async fn get_dns_resolver(&self) -> fidl_fuchsia_router_config::DnsResolverConfig {
        fidl_fuchsia_router_config::DnsResolverConfig::from(&self.dns_config)
    }
}

/// Log that the lif properties have changed.
///
/// This will be later use to update the operational state, but not the configuration state.
/// (we are not currently caching operational state, just querying for it).
fn log_property_change(lif: &mut lifmgr::LIF, iface: hal::Interface) {
    let properties = lif.properties();
    let new_properties: lifmgr::LIFProperties = iface.into();
    if properties != &new_properties {
        info!("Properties have changed {:?}: new properties {:?}", properties, new_properties);
    }
}

/// Represents the version of the configuration associated to a device object (i.e. an
/// interface, and ACL, etc). It should only be updated when configuration state is updated. It
/// should never be updated due to operational state changes.
///
/// For example, if an interface is configured to get its IP address via DHCP, the configuration
/// is changed to enable DHCP client on the interface. The address received from DHCP is an
/// operational state change. It is not to be saved in the configuration.
///
/// Adding a static neighbor entry to the neighbor table is a configuration change. Learning an
/// entry dynamically and adding it to the neighbor table is an operational change.
///
/// For a good definition of configuration vs. operational state, please see:
/// https://tools.ietf.org/html/rfc6244#section-4.3
type Version = u64;
type UUID = u128;

static ID: AtomicUsize = AtomicUsize::new(0);

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
    pub fn update(&mut self, version: Version) {
        self.version = version;
    }
    pub fn uuid(&self) -> UUID {
        self.uuid
    }
    pub fn version(&self) -> u64 {
        self.version
    }
}

impl From<ElementId> for fidl_fuchsia_router_config::Id {
    fn from(id: ElementId) -> Self {
        fidl_fuchsia_router_config::Id { uuid: id.uuid.to_ne_bytes(), version: id.version }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_net as net;
    use fuchsia_async as fasync;
    use std::net::IpAddr;

    #[test]
    fn test_elementid() {
        let uuid = generate_uuid();
        let version = 4;
        let new_version = 34;

        let mut e = ElementId { uuid, version };

        assert_eq!(e.uuid(), uuid);
        assert_eq!(e.version(), version);

        e.update(new_version);
        assert_eq!(e.version(), new_version);

        assert_eq!(
            fidl_fuchsia_router_config::Id::from(e),
            fidl_fuchsia_router_config::Id { uuid: uuid.to_ne_bytes(), version: new_version },
        );
    }

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

    fn net_interface_with_flags(
        port: u32,
        addr: [u8; 4],
        dhcp: bool,
        enabled: bool,
    ) -> netstack::NetInterface {
        netstack::NetInterface {
            id: port,
            flags: if enabled { netstack::NET_INTERFACE_FLAG_UP } else { 0 }
                | if dhcp { netstack::NET_INTERFACE_FLAG_DHCP } else { 0 },
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

    fn net_interface(port: u32, addr: [u8; 4]) -> netstack::NetInterface {
        net_interface_with_flags(port, addr, true, true)
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
        assert!(
            !device_state.port_manager.port_available(&hal::PortId::from(5)).unwrap(),
            "lif at port {:?}: {:?}",
            5,
            device_state.lif_manager.lif_at_port(5.into())
        );
        // Assert that network manager now knows about this new interface.
        {
            let lifs: Vec<&lifmgr::LIF> = device_state.lifs(LIFType::WAN).collect();
            assert_eq!(lifs.len(), 1);
            assert_eq!(lifs[0].pid().to_u64(), 5);
            // Also make sure that it is set as the NAT global IP.
            assert_eq!(
                device_state.service_manager.get_nat_config().global_ip,
                Some(LifIpAddr { address: IpAddr::from([1, 2, 3, 4]), prefix: 24 })
            );
        }

        // Netstack informs network manager of a new interface at port 4.
        assert!(device_state
            .update_state_for_netstack_event(netstack::NetstackEvent::OnInterfacesChanged {
                interfaces: vec![net_interface(4, [3, 4, 5, 6])],
            },)
            .await
            .is_ok());
        // Assert that network manager now knows about the new interface and updates to the other
        // interface.
        {
            let lifs: Vec<&lifmgr::LIF> = device_state.lifs(LIFType::WAN).collect();
            lifs.iter().find(|lif| lif.pid().to_u64() == 5).unwrap();
            lifs.iter().find(|lif| lif.pid().to_u64() == 4).unwrap();
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
                interfaces: vec![
                    net_interface_with_flags(5, [0, 0, 0, 0], false, true),
                    net_interface_with_flags(5, [0, 0, 0, 0], true, false),
                ],
            },)
            .await
            .is_ok());
        // Port should still be free.
        assert!(device_state.port_manager.port_available(&hal::PortId::from(5)).unwrap());
        assert_eq!(device_state.lifs(LIFType::WAN).next(), None);
    }
}
