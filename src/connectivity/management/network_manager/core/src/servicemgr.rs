// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Manages services

// In case we roll the toolchain and something we're using as a feature has been
// stabilized.
#![allow(stable_features)]
// TODO(dpradilla): reenable and add comments #![deny(missing_docs)]
#![deny(unreachable_patterns)]
#![deny(unused)]
// TODO(dpradilla): remove allow
#![allow(dead_code)]

use crate::hal::PortId;
use crate::lifmgr::{LifIpAddr, LIF};
use crate::{error, UUID};
use std::collections::HashSet;

pub struct NatConfig {
    pub enable: bool,
    pub local_subnet: Option<LifIpAddr>,
    pub global_ip: Option<LifIpAddr>,
    pub pid: Option<PortId>,
}

struct SecurityFeatures {
    nat: NatConfig,
}

/// `Manager` keeps track of interfaces where a service is enabled
/// and verifies conflicting services are not enabled.
pub struct Manager {
    // dhcp_server has collection of interfaces where DHCP dhcp_server is enabled.
    dhcp_server: std::collections::HashSet<UUID>,
    // dhcp_client has collection of interfaces DHCP dhcp_client is enabled.
    dhcp_client: std::collections::HashSet<UUID>,
    // security features.
    security: SecurityFeatures,
}

impl Manager {
    //! Creates a new Manager.
    pub fn new() -> Self {
        Manager {
            dhcp_server: HashSet::new(),
            dhcp_client: HashSet::new(),
            security: SecurityFeatures {
                nat: NatConfig { enable: false, local_subnet: None, global_ip: None, pid: None },
            },
        }
    }
    /// `enable_server` sets dhcp dhcp_server as enabled on indicated interface.
    pub fn enable_server(&mut self, lif: &LIF) -> error::Result<bool> {
        if self.dhcp_client.contains(&lif.id().uuid) {
            return Err(error::NetworkManager::SERVICE(error::Service::NotEnabled));
        }
        Ok(self.dhcp_server.insert(lif.id().uuid))
    }
    /// `disable_server` sets dhcp dhcp_server as disable on indicated interface.
    pub fn disable_server(&mut self, lif: &LIF) -> bool {
        self.dhcp_server.remove(&lif.id().uuid)
    }
    /// `is_server_enabled` returns true if the DHCP dhcp_server is enabled on indicated interface.
    pub fn is_server_enabled(&mut self, lif: &LIF) -> bool {
        self.dhcp_server.contains(&lif.id().uuid)
    }
    /// `enable_client` sets dhcp dhcp_client as enabled on indicated interface.
    pub fn enable_client(&mut self, lif: &LIF) -> error::Result<bool> {
        if self.dhcp_server.contains(&lif.id().uuid) {
            return Err(error::NetworkManager::SERVICE(error::Service::NotEnabled));
        }
        Ok(self.dhcp_client.insert(lif.id().uuid))
    }
    /// `disable_client` sets dhcp dhcp_client as disable on indicated interface.
    pub fn disable_client(&mut self, lif: &LIF) -> bool {
        self.dhcp_client.remove(&lif.id().uuid)
    }
    /// `is_client_enabled` returns true if the DHCP dhcp_client is enabled on indicated interface.
    pub fn is_client_enabled(&mut self, lif: &LIF) -> bool {
        self.dhcp_client.contains(&lif.id().uuid)
    }
    /// `is_nat_enabled` returns true if NAT is enabled.
    pub fn is_nat_enabled(&mut self) -> bool {
        self.security.nat.enable
    }
    /// `enable_nat` method indicates that NAT is now enabled.
    pub fn enable_nat(&mut self) {
        self.security.nat.enable = true;
    }
    /// `disable_nat` method indicates that NAT is now disabled.
    pub fn disable_nat(&mut self) {
        self.security.nat.enable = false;
    }
    /// `get_nat_config` returns the current NAT configuration.
    pub fn get_nat_config(&mut self) -> &mut NatConfig {
        &mut self.security.nat
    }
    /// `set_local_subnet_nat` sets the local subnet to be NATed.
    pub fn set_local_subnet_nat(&mut self, s: LifIpAddr) {
        self.security.nat.local_subnet = Some(s);
    }
    /// `set_global_ip_nat` sets the global IP to be NATed.
    pub fn set_global_ip_nat(&mut self, g: LifIpAddr, p: PortId) {
        self.security.nat.global_ip = Some(g);
        self.security.nat.pid = Some(p);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lifmgr::{LIFType, LifIpAddr, LIF};
    use crate::portmgr::PortId;
    use crate::portmgr::{Port, PortManager};

    fn create_lifs() -> (LIF, LIF) {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), "port1", 1));
        pm.add_port(Port::new(PortId::from(2), "port2", 1));
        let l1 =
            LIF::new(1, LIFType::WAN, "name1", PortId::from(0), vec![PortId::from(1)], 0, None)
                .unwrap();
        let l2 =
            LIF::new(2, LIFType::LAN, "name2", PortId::from(0), vec![PortId::from(2)], 0, None)
                .unwrap();
        (l1, l2)
    }

    #[test]
    fn test_service_manager_new() {
        let m = Manager::new();
        assert_eq!(m.dhcp_server.len(), 0);
        assert_eq!(m.dhcp_client.len(), 0);
    }

    #[test]
    fn test_server_enable() {
        let mut m = Manager::new();
        let (l, _) = create_lifs();
        assert!(m.enable_server(&l).unwrap_or(false));
        assert_eq!(m.dhcp_server.len(), 1);
    }

    #[test]
    fn test_server_double_enable() {
        let mut m = Manager::new();
        let (l, _) = create_lifs();
        assert!(m.enable_server(&l).unwrap_or(false));
        assert!(!m.enable_server(&l).unwrap_or(false));
        assert_eq!(m.dhcp_server.len(), 1);
    }

    #[test]
    fn test_disable_server() {
        let mut m = Manager::new();
        let (l, l2) = create_lifs();
        assert!(m.enable_server(&l).unwrap_or(false));
        assert_eq!(m.dhcp_server.len(), 1);
        assert!(m.disable_server(&l));
        assert_eq!(m.dhcp_server.len(), 0);

        assert!(m.enable_server(&l).unwrap_or(false));
        assert!(m.enable_server(&l2).unwrap_or(false));
        assert_eq!(m.dhcp_server.len(), 2);
        assert!(m.disable_server(&l));
        assert_eq!(m.dhcp_server.len(), 1);
    }

    #[test]
    fn test_server_disable_already_disabled() {
        let mut m = Manager::new();
        let (l, _) = create_lifs();
        assert!(!m.disable_server(&l));
        assert_eq!(m.dhcp_server.len(), 0);
    }

    #[test]
    fn test_client_enable() {
        let mut m = Manager::new();
        let (l, _) = create_lifs();
        assert!(m.enable_client(&l).unwrap_or(false));
        assert_eq!(m.dhcp_client.len(), 1);
    }

    #[test]
    fn test_client_double_enable() {
        let mut m = Manager::new();
        let (l, _) = create_lifs();
        assert!(m.enable_client(&l).unwrap_or(false));
        assert!(!m.enable_client(&l).unwrap_or(false));
        assert_eq!(m.dhcp_client.len(), 1);
    }

    #[test]
    fn test_disable_client() {
        let mut m = Manager::new();
        let (l, l2) = create_lifs();
        assert!(m.enable_client(&l).unwrap_or(false));
        assert_eq!(m.dhcp_client.len(), 1);
        assert!(m.disable_client(&l));
        assert_eq!(m.dhcp_client.len(), 0);

        assert!(m.enable_client(&l).unwrap_or(false));
        assert!(m.enable_client(&l2).unwrap_or(false));
        assert_eq!(m.dhcp_client.len(), 2);
        assert!(m.disable_client(&l));
        assert_eq!(m.dhcp_client.len(), 1);
    }

    #[test]
    fn test_client_disable_already_disabled() {
        let mut m = Manager::new();
        let (l, _) = create_lifs();
        assert!(!m.disable_client(&l));
        assert_eq!(m.dhcp_client.len(), 0);
    }

    #[test]
    fn test_enable_server_with_client_enabled() {
        let mut m = Manager::new();
        let (l, _) = create_lifs();
        assert!(m.enable_client(&l).unwrap_or(false));
        assert_eq!(m.dhcp_client.len(), 1);
        assert!(!m.enable_server(&l).unwrap_or(false));
        assert_eq!(m.dhcp_server.len(), 0);
    }

    #[test]
    fn test_enable_client_with_server_enabled() {
        let mut m = Manager::new();
        let (l, _) = create_lifs();
        assert!(m.enable_server(&l).unwrap_or(false));
        assert_eq!(m.dhcp_server.len(), 1);
        assert!(!m.enable_client(&l).unwrap_or(false));
        assert_eq!(m.dhcp_client.len(), 0);
    }

    #[test]
    fn test_nat_config() {
        let mut m = Manager::new();

        assert_eq!(m.get_nat_config().enable, false);
        assert_eq!(m.is_nat_enabled(), false);

        m.enable_nat();
        assert_eq!(m.is_nat_enabled(), true);

        m.disable_nat();
        assert_eq!(m.is_nat_enabled(), false);

        let lifip = LifIpAddr { address: "169.254.0.1".parse().unwrap(), prefix: 32 };
        m.set_local_subnet_nat(lifip.clone());
        assert_eq!(m.get_nat_config().local_subnet.as_ref().unwrap(), &lifip);

        let pid = PortId::from(1);
        m.set_global_ip_nat(lifip.clone(), pid);
        assert_eq!(m.get_nat_config().pid.unwrap(), pid);
    }
}
