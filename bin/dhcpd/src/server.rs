// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]
#![allow(unused)] // TODO(atait): Remove once there are non-test clients

use byteorder::{BigEndian, ByteOrder};
use protocol;
use protocol::{ConfigOption, Message, MessageType, OpCode, OptionCode};
use std::collections::{BTreeSet, HashMap, HashSet};
use std::net::Ipv4Addr;

/// A minimal DHCP server.
///
/// This comment will be expanded upon in future CLs as the server design
/// is iterated upon.
pub struct Server {
    cache: CachedClients,
    pool: AddressPool,
    config: ServerConfig,
}

impl Server {
    /// Returns an initialized `Server` value.
    pub fn new() -> Self {
        Server {
            cache: HashMap::new(),
            pool: AddressPool::new(),
            config: ServerConfig::new(),
        }
    }

    /// This is a placeholder method intended only for testing.
    pub fn add_addrs(&mut self, addrs: Vec<Ipv4Addr>) {
        self.pool.load_pool(addrs);
    }

    /// This is a placeholder method intended only for testing.
    pub fn set_config(&mut self, config: ServerConfig) {
        self.config = config
    }

    /// Dispatches an incoming DHCP message to the appropriate handler for processing.
    ///
    /// If the incoming message is a valid client DHCP message, then the server will attempt to
    /// take appropriate action to serve the client's request, update the internal server state,
    /// and return a response message. If the incoming message is invalid, or the server is
    /// unable to serve the request, then `dispatch()` will return `None`.
    pub fn dispatch(&mut self, msg: Message) -> Option<Message> {
        match msg.get_dhcp_type() {
            Some(MessageType::DHCPDISCOVER) => self.handle_discover(msg),
            Some(MessageType::DHCPREQUEST) => self.handle_request(msg),
            _ => None,
        }
    }

    fn handle_discover(&mut self, disc: Message) -> Option<Message> {
        let offered_ip = self.get_addr(&disc)?;
        let mut offer = build_offer(disc, &self.config);
        offer.yiaddr = offered_ip;
        self.update_server_cache(Ipv4Addr::from(offer.yiaddr), offer.chaddr, vec![]);

        Some(offer)
    }

    fn get_addr(&mut self, client: &Message) -> Option<Ipv4Addr> {
        if let Some(config) = self.cache.get(&client.chaddr) {
            if !config.expired {
                return Some(config.client_addr);
            } else if self.pool.addr_is_available(config.client_addr) {
                self.pool.allocate_addr(config.client_addr);
                return Some(config.client_addr);
            }
        }
        if let Some(opt) = client.get_config_option(OptionCode::RequestedIpAddr) {
            if opt.value.len() >= 4 {
                let requested_addr = protocol::ip_addr_from_buf_at(&opt.value, 0)
                    .expect("out of range indexing on opt.value");
                if self.pool.addr_is_available(requested_addr) {
                    self.pool.allocate_addr(requested_addr);
                    return Some(requested_addr);
                }
            }
        }
        self.pool.get_next_available_addr()
    }

    fn update_server_cache(
        &mut self, client_addr: Ipv4Addr, client_mac: MacAddr, client_opts: Vec<ConfigOption>,
    ) {
        let config = CachedConfig {
            client_addr: client_addr,
            options: client_opts,
            expired: false,
        };
        self.cache.insert(client_mac, config);
        self.pool.allocate_addr(client_addr);
    }

    fn handle_request(&mut self, req: Message) -> Option<Message> {
        match get_client_state(&req) {
            ClientState::Selecting => self.handle_request_selecting(req),
            ClientState::InitReboot => self.handle_request_init_reboot(req),
            ClientState::Renewing => self.handle_request_renewing(req),
            ClientState::Unknown => None,
        }
    }

    fn handle_request_selecting(&mut self, req: Message) -> Option<Message> {
        let requested_ip = req.ciaddr;
        if !is_recipient(self.config.server_ip, &req)
            || !is_assigned(&req, requested_ip, &self.cache, &self.pool)
        {
            return None;
        }
        Some(build_ack(req, requested_ip, &self.config))
    }

    fn handle_request_init_reboot(&mut self, req: Message) -> Option<Message> {
        let requested_ip = get_requested_ip_addr(&req)?;
        if !is_in_subnet(requested_ip, &self.config) {
            return Some(build_nak(req, &self.config));
        }
        if !is_client_mac_known(req.chaddr, &self.cache) {
            return None;
        }
        if !is_assigned(&req, requested_ip, &self.cache, &self.pool) {
            return Some(build_nak(req, &self.config));
        }
        Some(build_ack(req, requested_ip, &self.config))
    }

    fn handle_request_renewing(&mut self, req: Message) -> Option<Message> {
        let client_ip = req.ciaddr;
        if !is_assigned(&req, client_ip, &self.cache, &self.pool) {
            return None;
        }
        Some(build_ack(req, client_ip, &self.config))
    }
}

/// A cache mapping clients to their configuration data.
///
/// The server should store configuration data for all clients
/// to which it has sent a DHCPOFFER message. Entries in the cache
/// will eventually timeout, although such functionality is currently
/// unimplemented.
type CachedClients = HashMap<MacAddr, CachedConfig>;

type MacAddr = [u8; 6];

#[derive(Debug)]
struct CachedConfig {
    client_addr: Ipv4Addr,
    options: Vec<ConfigOption>,
    expired: bool,
}

impl CachedConfig {
    fn new() -> Self {
        CachedConfig {
            client_addr: Ipv4Addr::new(0, 0, 0, 0),
            options: vec![],
            expired: false,
        }
    }
}

/// The pool of addresses managed by the server.
///
/// Any address managed by the server should be stored in only one
/// of the available/allocated sets at a time. In other words, an
/// address in `available_addrs` must not be in `allocated_addrs` and
/// vice-versa.
#[derive(Debug)]
struct AddressPool {
    // available_addrs uses a BTreeSet so that addresses are allocated
    // in a deterministic order.
    available_addrs: BTreeSet<Ipv4Addr>,
    allocated_addrs: HashSet<Ipv4Addr>,
}

impl AddressPool {
    fn new() -> Self {
        AddressPool {
            available_addrs: BTreeSet::new(),
            allocated_addrs: HashSet::new(),
        }
    }

    fn load_pool(&mut self, addrs: Vec<Ipv4Addr>) {
        for addr in addrs {
            if !self.allocated_addrs.contains(&addr) {
                self.available_addrs.insert(addr);
            }
        }
    }

    fn get_next_available_addr(&self) -> Option<Ipv4Addr> {
        let mut iter = self.available_addrs.iter();
        match iter.next() {
            Some(addr) => Some(*addr),
            None => None,
        }
    }

    fn allocate_addr(&mut self, addr: Ipv4Addr) {
        self.available_addrs.remove(&addr);
        self.allocated_addrs.insert(addr);
    }

    fn addr_is_available(&self, addr: Ipv4Addr) -> bool {
        self.available_addrs.contains(&addr) && !self.allocated_addrs.contains(&addr)
    }

    fn addr_is_allocated(&self, addr: Ipv4Addr) -> bool {
        !self.available_addrs.contains(&addr) && self.allocated_addrs.contains(&addr)
    }
}

/// A collection of the basic configuration parameters needed by the server. 
#[derive(Debug)]
pub struct ServerConfig {
    /// The IPv4 address of the host running the server.
    pub server_ip: Ipv4Addr,
    /// The default time (in seconds) assigned to IP address leases assigned by the server.
    pub default_lease_time: u32,
    /// The number of bits to mask the subnet address from the host address in an IPv4Addr.
    pub subnet_mask: u8,
}

impl ServerConfig {
    fn new() -> Self {
        ServerConfig {
            server_ip: Ipv4Addr::new(0, 0, 0, 0),
            default_lease_time: 0,
            subnet_mask: 24,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ClientState {
    Unknown,
    Selecting,
    InitReboot,
    Renewing,
}

fn build_offer(client: Message, config: &ServerConfig) -> Message {
    let mut offer = client;
    offer.op = OpCode::BOOTREPLY;
    offer.secs = 0;
    offer.ciaddr = Ipv4Addr::new(0, 0, 0, 0);
    offer.siaddr = Ipv4Addr::new(0, 0, 0, 0);
    offer.sname = String::new();
    offer.file = String::new();
    add_required_options(&mut offer, &config);

    offer
}

fn add_required_options(offer: &mut Message, config: &ServerConfig) {
    offer.options.clear();
    let mut lease = vec![0; 4];
    BigEndian::write_u32(&mut lease, config.default_lease_time);
    offer.options.push(ConfigOption {
        code: OptionCode::IpAddrLeaseTime,
        value: lease,
    });
    offer.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPOFFER as u8],
    });
    offer.options.push(ConfigOption {
        code: OptionCode::ServerId,
        value: config.server_ip.octets().to_vec(),
    });
}

fn is_recipient(server_ip: Ipv4Addr, req: &Message) -> bool {
    if let Some(server_id) = get_server_id_from(&req) {
        return server_id == server_ip;
    }
    false
}

fn is_assigned(
    req: &Message, requested_ip: Ipv4Addr, cache: &CachedClients, pool: &AddressPool,
) -> bool {
    if let Some(client_config) = cache.get(&req.chaddr) {
        return client_config.client_addr == requested_ip && !client_config.expired
            && pool.addr_is_allocated(requested_ip);
    }
    false
}

fn build_ack(req: Message, requested_ip: Ipv4Addr, config: &ServerConfig) -> Message {
    let mut ack = req;
    ack.op = OpCode::BOOTREPLY;
    ack.secs = 0;
    ack.yiaddr = requested_ip;
    ack.options.clear();
    let mut lease = vec![0; 4];
    BigEndian::write_u32(&mut lease, config.default_lease_time);
    ack.options.push(ConfigOption {
        code: OptionCode::IpAddrLeaseTime,
        value: lease,
    });
    ack.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPACK as u8],
    });
    ack.options.push(ConfigOption {
        code: OptionCode::ServerId,
        value: config.server_ip.octets().to_vec(),
    });

    ack
}

fn is_in_subnet(ip: Ipv4Addr, config: &ServerConfig) -> bool {
    apply_subnet_mask_to(config.subnet_mask, ip)
        == apply_subnet_mask_to(config.subnet_mask, config.server_ip)
}

fn is_client_mac_known(mac: MacAddr, cache: &CachedClients) -> bool {
    cache.get(&mac).is_some()
}

fn build_nak(req: Message, config: &ServerConfig) -> Message {
    let mut nak = req;
    nak.op = OpCode::BOOTREPLY;
    nak.secs = 0;
    nak.ciaddr = Ipv4Addr::new(0, 0, 0, 0);
    nak.yiaddr = Ipv4Addr::new(0, 0, 0, 0);
    nak.siaddr = Ipv4Addr::new(0, 0, 0, 0);
    nak.options.clear();
    let mut lease = vec![0; 4];
    BigEndian::write_u32(&mut lease, config.default_lease_time);
    nak.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPNAK as u8],
    });
    nak.options.push(ConfigOption {
        code: OptionCode::ServerId,
        value: config.server_ip.octets().to_vec(),
    });

    nak
}

fn get_client_state(msg: &Message) -> ClientState {
    let maybe_server_id = get_server_id_from(&msg);
    let maybe_requested_ip = get_requested_ip_addr(&msg);
    let zero_ciaddr = Ipv4Addr::new(0, 0, 0, 0);

    if maybe_server_id.is_some()
        && maybe_requested_ip.is_none()
        && msg.ciaddr != zero_ciaddr {
        return ClientState::Selecting;
    } else if maybe_requested_ip.is_some() && msg.ciaddr == zero_ciaddr {
        return ClientState::InitReboot;
    } else if msg.ciaddr != zero_ciaddr {
        return ClientState::Renewing;
    } else {
        return ClientState::Unknown;
    }
}

fn get_requested_ip_addr(req: &Message) -> Option<Ipv4Addr> {
    let req_ip_opt = req.options
        .iter()
        .find(|opt| opt.code == OptionCode::RequestedIpAddr)?;
    let raw_ip = BigEndian::read_u32(&req_ip_opt.value);
    Some(Ipv4Addr::from(raw_ip))
}

fn get_server_id_from(req: &Message) -> Option<Ipv4Addr> {
    let server_id_opt = req.options
        .iter()
        .find(|opt| opt.code == OptionCode::ServerId)?;
    let raw_server_id = BigEndian::read_u32(&server_id_opt.value);
    Some(Ipv4Addr::from(raw_server_id))
}

fn apply_subnet_mask_to(prefix_len: u8, ip_addr: Ipv4Addr) -> Ipv4Addr {
    assert!(prefix_len < 32);
    let subnet_mask_bits = ::std::u32::MAX << (32 - prefix_len);
    let ip_addr_bits = BigEndian::read_u32(&ip_addr.octets());
    Ipv4Addr::from(ip_addr_bits & subnet_mask_bits)
}

#[cfg(test)]
mod tests {

    use super::*;
    use protocol::{ConfigOption, Message, MessageType, OpCode, OptionCode};
    use std::net::Ipv4Addr;

    fn new_test_server() -> Server {
        let mut server = Server::new();
        server.config.server_ip = Ipv4Addr::new(192, 168, 1, 1);
        server.config.default_lease_time = 42;
        server
            .pool
            .available_addrs
            .insert(Ipv4Addr::from([192, 168, 1, 2]));
        server
    }

    fn new_test_discover() -> Message {
        let mut disc = Message::new();
        disc.xid = 42;
        disc.chaddr = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
        disc.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPDISCOVER as u8],
        });
        disc
    }

    fn new_test_offer() -> Message {
        let mut offer = Message::new();
        offer.op = OpCode::BOOTREPLY;
        offer.xid = 42;
        offer.yiaddr = Ipv4Addr::new(192, 168, 1, 2);
        offer.chaddr = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
        offer.options.push(ConfigOption {
            code: OptionCode::IpAddrLeaseTime,
            value: vec![0, 0, 0, 42],
        });
        offer.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPOFFER as u8],
        });
        offer.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: vec![192, 168, 1, 1],
        });
        offer
    }

    fn new_test_request() -> Message {
        let mut req = Message::new();
        req.xid = 42;
        req.chaddr = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: vec![192, 168, 1, 2],
        });
        req.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPREQUEST as u8],
        });
        req.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: vec![192, 168, 1, 1],
        });
        req
    }

    fn new_test_ack() -> Message {
        let mut ack = Message::new();
        ack.op = OpCode::BOOTREPLY;
        ack.xid = 42;
        ack.yiaddr = Ipv4Addr::new(192, 168, 1, 2);
        ack.chaddr = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
        ack.options.push(ConfigOption {
            code: OptionCode::IpAddrLeaseTime,
            value: vec![0, 0, 0, 42],
        });
        ack.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPACK as u8],
        });
        ack.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: vec![192, 168, 1, 1],
        });
        ack
    }

    fn new_test_nak() -> Message {
        let mut nak = Message::new();
        nak.op = OpCode::BOOTREPLY;
        nak.xid = 42;
        nak.chaddr = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
        nak.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPNAK as u8],
        });
        nak.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: vec![192, 168, 1, 1],
        });
        nak
    }

    #[test]
    fn test_dispatch_with_discover_returns_correct_response() {
        let disc = new_test_discover();

        let mut server = new_test_server();
        let got = server.dispatch(disc).unwrap();

        let want = new_test_offer();

        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_discover_updates_server_state() {
        let disc = new_test_discover();
        let mac_addr = disc.chaddr;
        let mut server = new_test_server();
        let got = server.dispatch(disc).unwrap();

        assert_eq!(server.pool.available_addrs.len(), 0);
        assert_eq!(server.pool.allocated_addrs.len(), 1);
        assert_eq!(server.cache.len(), 1);
        let want_config = server.cache.get(&mac_addr).unwrap();
        assert_eq!(want_config.client_addr, Ipv4Addr::new(192, 168, 1, 2));
    }

    #[test]
    fn test_dispatch_with_discover_client_binding_returns_bound_addr() {
        let disc = new_test_discover();
        let mut server = new_test_server();
        let mut client_config = CachedConfig::new();
        client_config.client_addr = Ipv4Addr::new(192, 168, 1, 42);
        server.cache.insert(disc.chaddr, client_config);

        let got = server.dispatch(disc).unwrap();

        let mut want = new_test_offer();
        want.yiaddr = Ipv4Addr::new(192, 168, 1, 42);

        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_discover_expired_client_binding_returns_available_old_addr() {
        let disc = new_test_discover();
        let mut server = new_test_server();
        let mut client_config = CachedConfig::new();
        client_config.client_addr = Ipv4Addr::new(192, 168, 1, 42);
        client_config.expired = true;
        server.cache.insert(disc.chaddr, client_config);
        server
            .pool
            .available_addrs
            .insert(Ipv4Addr::new(192, 168, 1, 42));

        let got = server.dispatch(disc).unwrap();

        let mut want = new_test_offer();
        want.yiaddr = Ipv4Addr::new(192, 168, 1, 42);

        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_discover_unavailable_expired_client_binding_returns_new_addr() {
        let disc = new_test_discover();
        let mut server = new_test_server();
        let mut client_config = CachedConfig::new();
        client_config.client_addr = Ipv4Addr::new(192, 168, 1, 42);
        client_config.expired = true;
        server.cache.insert(disc.chaddr, client_config);
        server
            .pool
            .available_addrs
            .insert(Ipv4Addr::new(192, 168, 1, 2));
        server
            .pool
            .allocated_addrs
            .insert(Ipv4Addr::new(192, 168, 1, 42));

        let got = server.dispatch(disc).unwrap();

        let mut want = new_test_offer();
        want.yiaddr = Ipv4Addr::new(192, 168, 1, 2);

        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_discover_available_requested_addr_returns_requested_addr() {
        let mut disc = new_test_discover();
        disc.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: vec![192, 168, 1, 3],
        });

        let mut server = new_test_server();
        server
            .pool
            .available_addrs
            .insert(Ipv4Addr::new(192, 168, 1, 2));
        server
            .pool
            .available_addrs
            .insert(Ipv4Addr::new(192, 168, 1, 3));
        let got = server.dispatch(disc).unwrap();

        let mut want = new_test_offer();
        want.yiaddr = Ipv4Addr::new(192, 168, 1, 3);
        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_discover_unavailable_requested_addr_returns_next_addr() {
        let mut disc = new_test_discover();
        disc.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: vec![192, 168, 1, 42],
        });

        let mut server = new_test_server();
        server
            .pool
            .available_addrs
            .insert(Ipv4Addr::new(192, 168, 1, 2));
        server
            .pool
            .available_addrs
            .insert(Ipv4Addr::new(192, 168, 1, 3));
        server
            .pool
            .allocated_addrs
            .insert(Ipv4Addr::new(192, 168, 1, 42));
        let got = server.dispatch(disc).unwrap();

        let mut want = new_test_offer();
        want.yiaddr = Ipv4Addr::new(192, 168, 1, 2);
        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_selecting_request_valid_selecting_request_returns_ack() {
        let mut req = new_test_request();
        let requested_ip_addr = Ipv4Addr::new(192, 168, 1, 2);
        req.ciaddr = requested_ip_addr;
        req.options.remove(0);

        let mut server = new_test_server();
        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: requested_ip_addr,
                options: vec![],
                expired: false,
            },
        );
        server.pool.allocate_addr(requested_ip_addr);
        let got = server.dispatch(req).unwrap();

        let mut want = new_test_ack();
        want.ciaddr = requested_ip_addr;
        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_selecting_request_no_address_allocation_to_client_returns_none() {
        let mut req = new_test_request();
        req.ciaddr = Ipv4Addr::new(192, 168, 1, 2);
        req.options.remove(0);

        let mut server = new_test_server();
        let got = server.dispatch(req);

        assert!(got.is_none());
    }

    #[test]
    fn test_dispatch_with_selecting_request_wrong_server_id_returns_none() {
        let mut req = new_test_request();
        let requested_ip_addr = Ipv4Addr::new(192, 168, 1, 2);
        req.ciaddr = requested_ip_addr;
        req.options.remove(0);

        let mut server = new_test_server();
        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: requested_ip_addr,
                options: vec![],
                expired: false,
            },
        );
        server.pool.allocate_addr(requested_ip_addr);
        server.config.server_ip = Ipv4Addr::new(1, 2, 3, 4);
        let got = server.dispatch(req);

        assert!(got.is_none());
    }

    #[test]
    fn test_dispatch_with_selecting_request_valid_selecting_request_maintains_server_invariants() {
        let requested_ip_addr = Ipv4Addr::new(192, 168, 1, 2);
        let mut req = new_test_request();
        req.ciaddr = requested_ip_addr;
        req.options.remove(0);

        let mut server = new_test_server();
        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: requested_ip_addr,
                options: vec![],
                expired: false,
            },
        );
        server.pool.allocate_addr(requested_ip_addr);
        let _ = server.dispatch(req.clone()).unwrap();

        assert!(server.cache.contains_key(&req.chaddr));
        assert!(server.pool.addr_is_allocated(requested_ip_addr));
    }

    #[test]
    fn test_dispatch_with_selecting_request_no_address_allocation_maintains_server_invariants() {
        let requested_ip_addr = Ipv4Addr::new(192, 168, 1, 2);
        let mut req = new_test_request();
        req.ciaddr = requested_ip_addr;
        req.options.remove(0);

        let mut server = new_test_server();
        let _ = server.dispatch(req.clone());

        assert!(!server.cache.contains_key(&req.chaddr));
        assert!(!server.pool.addr_is_allocated(Ipv4Addr::new(192, 168, 1, 2)));
    }

    #[test]
    fn test_dispatch_with_init_boot_request_correct_address_returns_ack() {
        let mut req = new_test_request();
        req.options.remove(2);
        let requested_ip_addr = get_requested_ip_addr(&req).unwrap();

        let mut server = new_test_server();
        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: requested_ip_addr,
                options: vec![],
                expired: false,
            },
        );
        server.pool.allocate_addr(requested_ip_addr);
        let got = server.dispatch(req).unwrap();

        let want = new_test_ack();
        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_init_boot_request_incorrect_address_returns_nak() {
        let mut req = new_test_request();
        req.options.remove(0);
        req.options.remove(1);
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: vec![192, 168, 1, 42],
        });

        let mut server = new_test_server();
        let assigned_ip = Ipv4Addr::new(192, 168, 1, 2);
        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: assigned_ip,
                options: vec![],
                expired: false,
            },
        );
        server.pool.allocate_addr(assigned_ip);
        let got = server.dispatch(req).unwrap();

        let want = new_test_nak();
        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_init_boot_request_unknown_client_returns_none() {
        let mut req = new_test_request();
        req.options.remove(2);

        let mut server = new_test_server();
        let got = server.dispatch(req);

        assert!(got.is_none());
    }

    #[test]
    fn test_dispatch_with_init_boot_request_client_on_wrong_subnet_returns_nak() {
        let mut req = new_test_request();
        req.options.remove(0);
        req.options.remove(1);
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: vec![10, 0, 0, 1],
        });

        let mut server = new_test_server();
        let got = server.dispatch(req).unwrap();

        let want = new_test_nak();
        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_renewing_request_valid_request_returns_ack() {
        let mut req = new_test_request();
        req.options.remove(0);
        req.options.remove(1);
        let client_ip = Ipv4Addr::new(192, 168, 1, 2);
        req.ciaddr = client_ip;

        let mut server = new_test_server();
        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: client_ip,
                options: vec![],
                expired: false,
            },
        );
        server.pool.allocate_addr(client_ip);
        let got = server.dispatch(req).unwrap();

        let mut want = new_test_ack();
        want.ciaddr = client_ip;
        assert_eq!(got, want);
    }

    #[test]
    fn test_dispatch_with_renewing_request_unknown_client_returns_none() {
        let mut req = new_test_request();
        req.options.remove(0);
        req.options.remove(1);
        let client_ip = Ipv4Addr::new(192, 168, 1, 2);
        req.ciaddr = client_ip;

        let mut server = new_test_server();
        let got = server.dispatch(req);

        assert!(got.is_none());
    }

    #[test]
    fn test_get_client_state_with_selecting_returns_selecting() {
        let mut msg = new_test_request();
        msg.ciaddr = Ipv4Addr::new(192, 168, 1, 2);
        msg.options.remove(0);

        let got = get_client_state(&msg);

        assert_eq!(got, ClientState::Selecting);
    }

    #[test]
    fn test_get_client_state_with_initreboot_returns_initreboot() {
        let mut msg = new_test_request();
        msg.options.remove(2);

        let got = get_client_state(&msg);

        assert_eq!(got, ClientState::InitReboot);
    }

    #[test]
    fn test_get_client_state_with_renewing_returns_renewing() {
        let mut msg = new_test_request();
        msg.options.remove(0);
        msg.options.remove(1);
        msg.ciaddr = Ipv4Addr::new(1, 2, 3, 4);

        let got = get_client_state(&msg);

        assert_eq!(got, ClientState::Renewing);
    }

    #[test]
    fn test_get_client_state_with_unknown_returns_unknown() {
        let mut msg = new_test_request();
        msg.options.clear();

        let got = get_client_state(&msg);

        assert_eq!(got, ClientState::Unknown);
    }
}
