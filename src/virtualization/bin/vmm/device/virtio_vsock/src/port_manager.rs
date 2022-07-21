// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::connection::VsockConnectionKey,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    std::collections::{hash_map::Entry, HashMap, HashSet, VecDeque},
};

type HostPort = u32;
type GuestPort = u32;

// Ephemeral range taken from:
// https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml
const FIRST_EPHEMERAL_PORT: HostPort = 49152;
const LAST_EPHEMERAL_PORT: HostPort = 65535;

// This is an arbitrarily chosen length of time which can be adjusted up or down as needed.
const QUARANTINE_TIME: zx::Duration = zx::Duration::from_seconds(10);

struct HostPortInfo {
    has_listener: bool,
    guest_ports: HashSet<GuestPort>,
}

impl HostPortInfo {
    fn new() -> Self {
        HostPortInfo { has_listener: false, guest_ports: HashSet::new() }
    }
}

#[derive(Clone, Copy, Debug)]
struct QuarantinedConnection {
    connection: VsockConnectionKey,
    available_time: fasync::Time,
}

impl QuarantinedConnection {
    fn new(connection: VsockConnectionKey) -> Self {
        let available_time = fasync::Time::after(QUARANTINE_TIME);
        QuarantinedConnection { connection, available_time }
    }
}

pub struct PortManager {
    // Active ports tracked by the device. Multiple connections can be multiplexed over a single
    // host port, so a port is active as long as there is at least one connection or listener.
    active_ports: HashMap<HostPort, HostPortInfo>,

    // Connections that have been force shutdown (peer sends reset before the other side sent
    // shutdown) have not ended cleanly, so they are quarantined for a set amount of time to
    // prevent races on new, unrelated connections.
    quarantined_connections: VecDeque<QuarantinedConnection>,

    // The ephemeral port to start searching on, which is set to one past the last free port found.
    // This is used as a hint when searching for the next free port.
    ephemeral_port_start_search: HostPort,
}

impl PortManager {
    pub fn new() -> Self {
        PortManager {
            active_ports: HashMap::new(),
            quarantined_connections: VecDeque::new(),
            ephemeral_port_start_search: FIRST_EPHEMERAL_PORT,
        }
    }

    // Attempts to listen on a port. If a client is already listening on this port, returns
    // zx::Status::ALREADY_BOUND.
    pub fn add_listener(&mut self, port: HostPort) -> Result<(), zx::Status> {
        let entry = self.active_ports.entry(port).or_insert(HostPortInfo::new());
        if entry.has_listener {
            Err(zx::Status::ALREADY_BOUND)
        } else {
            entry.has_listener = true;
            Ok(())
        }
    }

    // Stops listening on a port. If there are no active connections, this port can immediately
    // be reused.
    pub fn remove_listener(&mut self, port: HostPort) {
        let result = match self.active_ports.entry(port) {
            Entry::Vacant(_) => Err(zx::Status::NOT_FOUND),
            Entry::Occupied(mut entry) => {
                if entry.get().has_listener {
                    entry.get_mut().has_listener = false;
                    if entry.get().guest_ports.is_empty() {
                        // There was a listener on this port without any connections, so the port
                        // can immediately be reused.
                        entry.remove_entry();
                    }
                    Ok(())
                } else {
                    Err(zx::Status::NOT_FOUND)
                }
            }
        };

        if result.is_err() {
            panic!("Attempted to stop listening on port {} which had no active listener", port);
        }
    }

    // Attempts to add a unique host/guest pair. If the connection already exists (including if the
    // connection is quarantined), returns zx::Status::ALREADY_EXISTS.
    pub fn add_connection(&mut self, connection: VsockConnectionKey) -> Result<(), zx::Status> {
        self.check_quarantined_connections();
        let entry = self.active_ports.entry(connection.host_port).or_insert(HostPortInfo::new());
        if entry.guest_ports.contains(&connection.guest_port) {
            Err(zx::Status::ALREADY_EXISTS)
        } else {
            entry.guest_ports.insert(connection.guest_port);
            Ok(())
        }
    }

    // Removes an active connection without quarantining it.
    pub fn remove_connection(&mut self, connection: VsockConnectionKey) {
        if let Err(_) = self.remove_connection_from_active(connection) {
            panic!("Attempted to remove untracked connection: {:?}", connection);
        }
    }

    // Removes and quarantines a connection. This connection stays active until leaving quarantine,
    // so until then no duplicate connections can be made and this port pair cannot be reused.
    pub fn remove_connection_unclean(&mut self, connection: VsockConnectionKey) {
        self.quarantined_connections.push_back(QuarantinedConnection::new(connection));
    }

    // Attempts to find an unused port from the ephemeral range. If none are available, returns
    // zx::Status::NO_RESOURCES.
    pub fn find_unused_ephemeral_port(&mut self) -> Result<HostPort, zx::Status> {
        match self.find_unused_port_in_range(
            FIRST_EPHEMERAL_PORT,
            LAST_EPHEMERAL_PORT,
            self.ephemeral_port_start_search,
        ) {
            Ok(port) => {
                self.ephemeral_port_start_search =
                    if port == LAST_EPHEMERAL_PORT { FIRST_EPHEMERAL_PORT } else { port + 1 };
                Ok(port)
            }
            err => err,
        }
    }

    // Allocates the first unused port between start and end, inclusive. Starts at hint which must
    // be within the defined range. Returns zx::Status::NO_RESOURCES if all ports are in use.
    fn find_unused_port_in_range(
        &mut self,
        start: HostPort,
        end: HostPort,
        hint: HostPort,
    ) -> Result<HostPort, zx::Status> {
        assert!(hint >= start && hint <= end);
        self.check_quarantined_connections();
        let mut current_port = hint;
        loop {
            if !self.active_ports.contains_key(&current_port) {
                return Ok(current_port);
            }

            current_port = if current_port == end { start } else { current_port + 1 };
            if current_port == hint {
                return Err(zx::Status::NO_RESOURCES);
            }
        }
    }

    // Removes a connection from the active connection map, returning zx::Status::NOT_FOUND if the
    // connection was not present.
    fn remove_connection_from_active(
        &mut self,
        connection: VsockConnectionKey,
    ) -> Result<(), zx::Status> {
        match self.active_ports.entry(connection.host_port) {
            Entry::Vacant(_) => Err(zx::Status::NOT_FOUND),
            Entry::Occupied(mut entry) => {
                if entry.get().guest_ports.contains(&connection.guest_port) {
                    entry.get_mut().guest_ports.remove(&connection.guest_port);
                    if entry.get().guest_ports.is_empty() && !entry.get().has_listener {
                        // No listener and no remaining connections, so this port can be
                        // immediately reused.
                        entry.remove_entry();
                    }
                    Ok(())
                } else {
                    Err(zx::Status::NOT_FOUND)
                }
            }
        }
    }

    // Frees connections that have been quarantined for enough time. This should happen before
    // attempting to allocate ports.
    fn check_quarantined_connections(&mut self) {
        let now = fasync::Time::now();
        while !self.quarantined_connections.is_empty() {
            let front = self.quarantined_connections.front().unwrap().clone();
            if front.available_time < now {
                if let Err(_) = self.remove_connection_from_active(front.connection) {
                    panic!(
                        "A quarantined connection was removed from active ports before its \
                        quarantine ended: {:?}",
                        front
                    );
                }

                self.quarantined_connections.pop_front();
            } else {
                break;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_virtualization::{DEFAULT_GUEST_CID, HOST_CID},
        fuchsia_async::TestExecutor,
    };

    #[fuchsia::test]
    async fn listen_on_ports() {
        let mut port_manager = PortManager::new();

        assert!(port_manager.add_listener(12345).is_ok());
        assert!(port_manager.active_ports.get(&12345).unwrap().has_listener);

        assert!(port_manager.add_listener(54321).is_ok());
        assert!(port_manager.active_ports.get(&54321).unwrap().has_listener);
    }

    #[fuchsia::test]
    async fn multiple_listen_on_single_port() {
        let mut port_manager = PortManager::new();

        assert!(port_manager.add_listener(12345).is_ok());
        assert_eq!(port_manager.add_listener(12345).unwrap_err(), zx::Status::ALREADY_BOUND);

        // The original listener is still set.
        assert!(port_manager.active_ports.get(&12345).unwrap().has_listener);
    }

    #[test]
    fn listen_unlisten_listen_on_port() {
        let executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));
        let mut port_manager = PortManager::new();

        // No need to progress time -- listeners are not quarantined when removed
        // as the state is easily synchronized via FIDL channel.
        assert!(port_manager.add_listener(12345).is_ok());
        port_manager.remove_listener(12345);
        assert!(port_manager.add_listener(12345).is_ok());
    }

    #[fuchsia::test]
    async fn port_stays_active_after_unlistening_with_active_connections() {
        let mut port_manager = PortManager::new();

        assert!(port_manager.add_listener(12345).is_ok());
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 12345, DEFAULT_GUEST_CID, 54321))
            .is_ok());
        port_manager.remove_listener(12345);

        // Port is still active as there's an active connection.
        assert!(port_manager.active_ports.contains_key(&12345));
    }

    #[fuchsia::test]
    async fn clean_connection_shutdown_does_not_quarantine() {
        let mut port_manager = PortManager::new();

        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 12345, DEFAULT_GUEST_CID, 54321))
            .is_ok());
        port_manager.remove_connection(VsockConnectionKey::new(
            HOST_CID,
            12345,
            DEFAULT_GUEST_CID,
            54321,
        ));

        assert!(!port_manager.active_ports.contains_key(&12345));
        assert!(port_manager.quarantined_connections.is_empty());
    }

    #[fuchsia::test]
    async fn connection_pair_already_in_use() {
        let mut port_manager = PortManager::new();

        // All three of these are fine -- ports can be multiplexed but the connection pair must be
        // unique.
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 2))
            .is_ok());
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 3))
            .is_ok());
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 3, DEFAULT_GUEST_CID, 1))
            .is_ok());

        // This connection is a duplicate, and is thus rejected.
        assert_eq!(
            port_manager
                .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 2))
                .unwrap_err(),
            zx::Status::ALREADY_EXISTS
        );
    }

    #[test]
    fn port_stays_active_when_connection_quarantined() {
        let executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));
        let mut port_manager = PortManager::new();

        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 2))
            .is_ok());
        port_manager.remove_connection_unclean(VsockConnectionKey::new(
            HOST_CID,
            1,
            DEFAULT_GUEST_CID,
            2,
        ));

        // Still in quarantine.
        assert_eq!(
            port_manager
                .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 2))
                .unwrap_err(),
            zx::Status::ALREADY_EXISTS
        );
    }

    #[test]
    fn port_stays_active_when_no_connections_but_listener() {
        let executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));
        let mut port_manager = PortManager::new();

        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 2))
            .is_ok());
        assert!(port_manager.add_listener(1).is_ok());

        // Both connection and listener are on the same port.
        assert_eq!(port_manager.active_ports.len(), 1);
        assert_eq!(port_manager.active_ports.get(&1).unwrap().guest_ports.len(), 1);

        port_manager.remove_connection_unclean(VsockConnectionKey::new(
            HOST_CID,
            1,
            DEFAULT_GUEST_CID,
            2,
        ));

        // One nano after quarantine ends.
        executor.set_fake_time(fuchsia_async::Time::after(
            QUARANTINE_TIME + zx::Duration::from_nanos(1),
        ));

        // Port is still in use due to the listener (need to check quarantined connections
        // explicitly as this is usually only checked when calling a public function).
        port_manager.check_quarantined_connections();
        assert_eq!(port_manager.active_ports.len(), 1);
        assert!(port_manager.active_ports.get(&1).unwrap().guest_ports.is_empty());
    }

    #[test]
    fn connection_pair_recycled_after_quarantine() {
        let executor = TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(fuchsia_async::Time::from_nanos(0));
        let mut port_manager = PortManager::new();

        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 2))
            .is_ok());
        port_manager.remove_connection_unclean(VsockConnectionKey::new(
            HOST_CID,
            1,
            DEFAULT_GUEST_CID,
            2,
        ));

        // One nano after quarantine ends.
        executor.set_fake_time(fuchsia_async::Time::after(
            QUARANTINE_TIME + zx::Duration::from_nanos(1),
        ));

        // Can re-use the now unquarantined connection.
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 2))
            .is_ok());
    }

    #[fuchsia::test]
    async fn find_ephemeral_ports() {
        let mut port_manager = PortManager::new();

        let port = port_manager.find_unused_ephemeral_port().unwrap();
        assert_eq!(port, FIRST_EPHEMERAL_PORT);
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, port, DEFAULT_GUEST_CID, 2))
            .is_ok());

        let port = port_manager.find_unused_ephemeral_port().unwrap();
        assert_eq!(port, FIRST_EPHEMERAL_PORT + 1);
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, port, DEFAULT_GUEST_CID, 2))
            .is_ok());

        port_manager.remove_connection(VsockConnectionKey::new(
            HOST_CID,
            FIRST_EPHEMERAL_PORT,
            DEFAULT_GUEST_CID,
            2,
        ));

        // Even though the first ephemeral port is now free, the port manager hints based on the
        // last used ephemeral port.
        let port = port_manager.find_unused_ephemeral_port().unwrap();
        assert_eq!(port, FIRST_EPHEMERAL_PORT + 2);
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, port, DEFAULT_GUEST_CID, 2))
            .is_ok());
    }

    #[fuchsia::test]
    async fn no_unused_ports_in_range() {
        let mut port_manager = PortManager::new();

        // Use host ports 0 to 5, inclusive.
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 0, DEFAULT_GUEST_CID, 2))
            .is_ok());
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 1, DEFAULT_GUEST_CID, 2))
            .is_ok());
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 2, DEFAULT_GUEST_CID, 2))
            .is_ok());
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 3, DEFAULT_GUEST_CID, 2))
            .is_ok());
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 4, DEFAULT_GUEST_CID, 2))
            .is_ok());
        assert!(port_manager
            .add_connection(VsockConnectionKey::new(HOST_CID, 5, DEFAULT_GUEST_CID, 2))
            .is_ok());

        assert_eq!(
            port_manager.find_unused_port_in_range(0, 5, 0).unwrap_err(),
            zx::Status::NO_RESOURCES
        );
    }
}
