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
    /// Creates a physical port to be managed by the network manager.
    pub fn new(port_id: PortId, path: &str, v: Version) -> Self {
        //TODO(dpradilla) this has to check port actually exists and reference to it.
        Port { e_id: ElementId::new(v), port_id, path: path.to_string() }
    }
}

/// `PortManager` keeps track of physical ports in the system.
#[derive(Default)]
pub struct PortManager {
    /// `ports` keeps track of ports in the system and if they are available or not.
    ports: std::collections::HashMap<PortId, (Port, bool)>,
    /// maps a path to a port id.
    path_to_pid: std::collections::HashMap<String, PortId>,
}

impl PortManager {
    /// Creates a new PortManager.
    pub fn new() -> Self {
        PortManager { ports: HashMap::new(), path_to_pid: HashMap::new() }
    }

    /// Adds a physical port to be managed by network manager.
    pub fn add_port(&mut self, p: Port) {
        // When adding a new port, it is considered available as no one has yet used it.
        // If the port already exists, update the port, but keep availability unchanged.

        let pid = p.port_id;
        let path = p.path.clone();
        let available =
            if let Some((_, available)) = self.ports.get(&p.port_id) { *available } else { true };
        self.ports.insert(p.port_id, (p, available));
        self.path_to_pid.insert(path, pid);
    }

    /// Removes a port from port manager.
    pub fn remove_port(&mut self, id: PortId) -> Option<Port> {
        let portopt = self.ports.remove(&id).map(|(p, _)| p);
        if let Some(port) = portopt.clone() {
            if self.path_to_pid.remove(&port.path).is_none() {
                // There is no action that the caller can do in this case,
                // therefore just logging it as a warning.
                // Removing the entry as we were asked will succeed,
                // and also remove the inconsistency.
                warn!("PortManager in inconsistent state {:?}", port);
            }
        }
        portopt
    }

    /// Returns information about a port in port manager.
    pub fn port(&self, id: PortId) -> Option<&Port> {
        let (p, _) = self.ports.get(&id)?;
        Some(&p)
    }

    /// Returns port id from a topo path.
    pub fn port_id(&self, path: &str) -> Option<&PortId> {
        self.path_to_pid.get(path)
    }

    /// Returns topo path from a port id.
    pub fn topo_path(&self, pid: &PortId) -> Option<&str> {
        self.path_to_pid
            .iter()
            .filter_map(|(k, v)| if pid == v { Some(k.as_str()) } else { None })
            .next()
    }

    /// Returns all ports known by port manager.
    pub fn ports(&self) -> impl ExactSizeIterator<Item = &Port> {
        self.ports.iter().map(|(_, (p, _))| p)
    }

    /// Marks a port as in use.
    ///
    /// Returns true on success, otherwise returns false if the port is already in use or doesn't
    /// exist.
    pub fn use_port(&mut self, id: PortId) -> bool {
        if let Some((_, available)) = self.ports.get_mut(&id) {
            if *available {
                // Make it unavailable.
                *available = false;
                return true;
            }
        }
        false
    }

    /// Marks a port as available, if in use.
    ///
    /// Returns true on success, otherwise returns false if the port does not exist or is not in
    /// use.
    pub fn release_port(&mut self, id: PortId) -> bool {
        if let Some((_, available)) = self.ports.get_mut(&id) {
            if !*available {
                // Make it available.
                *available = true;
                return true;
            }
        }
        false
    }

    /// Checks if the port is currently in use by a valid interface. Returns None if no such port
    /// exists.
    #[cfg(test)]
    pub fn port_available(&self, port: &PortId) -> Option<bool> {
        self.ports
            .iter()
            .find_map(|(id, (_, available))| if port == id { Some(*available) } else { None })
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
        let got = pm.use_port(PortId::from(5));
        assert_eq!(got, true)
    }

    #[test]
    fn test_port_manager_double_use() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        let got = pm.use_port(PortId::from(5));
        assert_eq!(got, true);
        let got2 = pm.use_port(PortId::from(5));
        assert_eq!(got2, false)
    }

    #[test]
    fn test_port_manager_use_inexisting() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        pm.add_port(Port::new(PortId::from(3), &generate_path(3), 1));
        let got = pm.use_port(PortId::from(15));
        assert_eq!(got, false)
    }

    #[test]
    fn test_port_id() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        let got = pm.port_id(&generate_path(1));
        assert_eq!(got, Some(&PortId::from(1)));
        let got = pm.port_id(&generate_path(5));
        assert_eq!(got, Some(&PortId::from(5)));
        let got = pm.port_id(&generate_path(6));
        assert_eq!(got, None);
    }

    #[test]
    fn test_topo_path() {
        let mut pm = PortManager::new();
        pm.add_port(Port::new(PortId::from(1), &generate_path(1), 1));
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));
        let got = pm.topo_path(&PortId::from(1));
        assert_eq!(got, Some(generate_path(1).as_str()));
        let got = pm.topo_path(&PortId::from(5));
        assert_eq!(got, Some(generate_path(5).as_str()));
        let got = pm.topo_path(&PortId::from(6));
        assert_eq!(got, None);
    }

    #[test]
    fn test_port_id_after_port_removed() {
        let mut pm = PortManager::new();

        let p = Port::new(PortId::from(1), &generate_path(1), 1);
        pm.add_port(p.clone());
        pm.add_port(Port::new(PortId::from(5), &generate_path(5), 1));

        let got = pm.port_id(&generate_path(1));
        assert_eq!(got, Some(&PortId::from(1)));
        let got = pm.port_id(&generate_path(5));
        assert_eq!(got, Some(&PortId::from(5)));

        let got = pm.remove_port(PortId::from(1));
        assert_eq!(got, Some(p));

        let got = pm.port_id(&generate_path(5));
        assert_eq!(got, Some(&PortId::from(5)));
        let got = pm.port_id(&generate_path(1));
        assert_eq!(got, None);
    }
}
