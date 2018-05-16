// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]
#![allow(unused)] // TODO(atait): Remove once there are non-test clients

use byteorder::{BigEndian, ByteOrder};
use protocol::{ConfigOption, Message, MessageType, OpCode, OptionCode};
use std::collections::{BTreeSet, HashMap, HashSet};
use std::net::Ipv4Addr;

/// A minimal DHCP server.
///
/// This comment will be expanded upon in future CLs as the server design
/// is iterated upon.
pub struct Server {
    client_configs_cache: HashMap<MacAddr, CachedConfig>,
    addr_pool: AddressPool,
    server_config: ServerConfig,
}

impl Server {
    /// Returns an initialized `Server` value.
    pub fn new() -> Self {
        Server {
            client_configs_cache: HashMap::new(),
            addr_pool: AddressPool::new(),
            server_config: ServerConfig::new(),
        }
    }

    fn handle_discover_message(&mut self, disc_msg: Message) -> Option<Message> {
        let offer_msg = self.generate_offer_message(disc_msg)?;
        self.update_server_cache(Ipv4Addr::from(offer_msg.yiaddr), offer_msg.chaddr, vec![]);

        Some(offer_msg)
    }

    fn generate_offer_message(&mut self, disc_msg: Message) -> Option<Message> {
        let mut offer_msg = disc_msg;
        offer_msg.op = OpCode::BOOTREPLY;
        offer_msg.secs = 0;
        offer_msg.ciaddr = [0; 4];
        offer_msg.siaddr = [0; 4];
        offer_msg.sname = String::new();
        offer_msg.file = String::new();
        let next_addr = self.addr_pool.get_next_available_addr()?;
        offer_msg.yiaddr = next_addr.octets();
        self.add_required_options_to(&mut offer_msg);
        Some(offer_msg)
    }

    fn add_required_options_to(&self, offer_msg: &mut Message) {
        offer_msg.options.clear();
        let mut lease = vec![0; 4];
        BigEndian::write_u32(&mut lease, self.server_config.default_lease_time);
        offer_msg.options.push(ConfigOption {
            code: OptionCode::IpAddrLeaseTime,
            value: lease,
        });
        offer_msg.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPOFFER as u8],
        });
        offer_msg.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: self.server_config.server_ip.octets().to_vec(),
        });
    }

    fn update_server_cache(
        &mut self, client_addr: Ipv4Addr, client_mac: MacAddr, client_opts: Vec<ConfigOption>,
    ) {
        let config = CachedConfig {
            client_addr: client_addr,
            options: client_opts,
            expired: false,
        };
        self.client_configs_cache.insert(client_mac, config);
        self.addr_pool.allocate_addr(client_addr);
    }
}

#[test]
fn test_handle_discover_returns_correct_response() {
    let mut disc_msg = Message::new();
    disc_msg.xid = 42;
    disc_msg.chaddr = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];

    let mut server = Server::new();
    server.server_config.server_ip = Ipv4Addr::new(192, 168, 1, 1);
    server.server_config.default_lease_time = 42;
    server
        .addr_pool
        .available_addrs
        .insert(Ipv4Addr::from([192, 168, 1, 2]));
    let got = server.handle_discover_message(disc_msg).unwrap();

    let mut want = Message::new();
    want.op = OpCode::BOOTREPLY;
    want.xid = 42;
    want.yiaddr = [192, 168, 1, 2];
    want.chaddr = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF];
    want.options.push(ConfigOption {
        code: OptionCode::IpAddrLeaseTime,
        value: vec![0, 0, 0, 42],
    });
    want.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPOFFER as u8],
    });
    want.options.push(ConfigOption {
        code: OptionCode::ServerId,
        value: vec![192, 168, 1, 1],
    });

    assert_eq!(got, want);
    assert_eq!(server.addr_pool.available_addrs.len(), 0);
    assert_eq!(server.addr_pool.allocated_addrs.len(), 1);
    assert_eq!(server.client_configs_cache.len(), 1);
    let want_config = server.client_configs_cache.get(&want.chaddr).unwrap();
    assert_eq!(want_config.client_addr, Ipv4Addr::new(192, 168, 1, 2));
}

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
}

#[derive(Debug)]
struct ServerConfig {
    server_ip: Ipv4Addr,
    default_lease_time: u32,
}

impl ServerConfig {
    fn new() -> Self {
        ServerConfig {
            server_ip: Ipv4Addr::new(0, 0, 0, 0),
            default_lease_time: 0,
        }
    }
}
