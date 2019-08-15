// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple port manager.

use crate::hal;
use crate::{ElementId, Version};
use std::collections::HashMap;

pub type PortId = hal::PortId;

#[derive(Eq, PartialEq, Debug, Clone)]
pub struct Port {
    pub e_id: ElementId,
    pub port_id: PortId,
    pub path: String,
}

impl Port {
    /// new creates a physical port to be managed by the router manager.
    pub fn new(port_id: PortId, path: &str, v: Version) -> Self {
        //TODO(dpradilla) this has to check port actually exists and reference to it.
        Port { e_id: ElementId::new(v), port_id, path: path.to_string() }
    }
}

/// PortManager keeps track of physical ports in the system.
pub struct PortManager {
    // ports keeps track of ports in the system and if they are available or not.
    ports: std::collections::HashMap<PortId, (Port, bool)>,
}

impl PortManager {
    /// Creates a new PortManager.
    pub fn new() -> Self {
        PortManager { ports: HashMap::new() }
    }
    /// add_ports adds a physical port to be manager by router manager.
    pub fn add_port(&mut self, p: Port) {
        // When adding a new port, is is considered available as no one has yet used it.
        // If the port already exists, update the port, but keep availability unchanged.
        let available =
            if let Some((_, available)) = self.ports.get(&p.port_id) { *available } else { true };
        self.ports.insert(p.port_id, (p, available));
    }
    /// remove_ports removes a port from port manager.
    pub fn remove_port(&mut self, id: PortId) -> Option<Port> {
        self.ports.remove(&id).map(|(p, _)| p)
    }
    /// port gets information about a port in port manager.
    pub fn port(&self, id: PortId) -> Option<&Port> {
        let (p, _) = self.ports.get(&id)?;
        Some(&p)
    }
    /// ports returns all ports known by port manager.
    pub fn ports(&self) -> impl ExactSizeIterator<Item = &Port> {
        self.ports.iter().map(|(_, (p, _))| p)
    }
    /// use_port marks a port as in use, and returns true in that case.
    /// It returns false if the port is already in use or doesnt exist.
    pub fn use_port(&mut self, id: &PortId) -> bool {
        if let Some((_, available)) = self.ports.get_mut(id) {
            if *available {
                // Make it unavailable.
                *available = false;
                return true;
            }
        }
        false
    }
    /// release_port makes a port available if in use.
    /// it does nothing if the port does not exist.
    pub fn release_port(&mut self, id: &PortId) {
        if let Some((_, available)) = self.ports.get_mut(id) {
            if !*available {
                // Make it available.
                *available = true;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn generate_path(id: u32) -> String {
        "port".to_owned() + &id.to_string()
    }

    #[test]
    fn test_new_port() {
        let p = Port::new(PortId::from(1), &generate_path(1), 123);
        assert_eq!(p.port_id, PortId::from(1));
        assert_eq!(p.path, "port1");
        assert_eq!(p.e_id.version, 123);
        let p = Port::new(PortId::from(20), &generate_path(20), 95);
        assert_eq!(p.port_id, PortId::from(20));
        assert_eq!(p.path, "port20");
        assert_eq!(p.e_id.version, 95);
    }

    #[test]
    fn test_port_manager_new() {
        let pm = PortManager::new();
        assert_eq!(pm.ports.len(), 0);
    }

    #[test]
    fn test_port_manager_add() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(2), &generate_path(2), 1));
        assert_eq!(pm.ports.len(), 3);
    }

    #[test]
    fn test_port_manager_add_existing() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        assert_eq!(pm.ports.len(), 3);
    }

    #[test]
    fn test_port_manager_get_existing() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        let p = Port::new(PortId::from(5), &generate_path(5), 1);
        pm.add_port(p.clone());
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        let got = pm.port(PortId::from(5));
        assert_eq!(got, Some(&p))
    }

    #[test]
    fn test_port_manager_get_inexisting() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        let got = pm.port(PortId::from(15));
        assert_eq!(got, None)
    }

    #[test]
    fn test_port_manager_update_existing() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        assert_eq!(pm.ports.len(), 1);
        let p = pm.port(PortId::from(1)).unwrap();
        assert_eq!(p.path, generate_path(1));
        assert_eq!(p.port_id, PortId::from(1));
        pm.add_port(Port::new(PortId::from(1), &generate_path(9), 9));
        assert_eq!(pm.ports.len(), 1);
        let p = pm.port(PortId::from(1)).unwrap();
        assert_eq!(p.path, generate_path(9));
        assert_eq!(p.port_id, PortId::from(1));
    }

    #[test]
    fn test_port_manager_remove_existing() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        let p = Port::new(PortId::from(5), &generate_path(5), 1);
        pm.add_port(p.clone());
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));

        let got = pm.remove_port(PortId::from(5));

        assert_eq!(pm.ports.len(), 2);
        assert_eq!(got, Some(p))
    }

    #[test]
    fn test_port_manager_remove_inexisting() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        assert_eq!(pm.ports.len(), 3);

        let got = pm.remove_port(PortId::from(6));

        assert_eq!(pm.ports.len(), 3);
        assert_eq!(got, None)
    }

    #[test]
    fn test_port_manager_use() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        let got = pm.use_port(&PortId::from(5));
        assert_eq!(got, true)
    }

    #[test]
    fn test_port_manager_double_use() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        let got = pm.use_port(&PortId::from(5));
        assert_eq!(got, true);
        let got2 = pm.use_port(&PortId::from(5));
        assert_eq!(got2, false)
    }

    #[test]
    fn test_port_manager_use_inexisting() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        let got = pm.use_port(&PortId::from(15));
        assert_eq!(got, false)
    }
}
