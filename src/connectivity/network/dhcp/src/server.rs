// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::configuration::{ClientConfig, ServerConfig};
use crate::protocol::{self, ConfigOption, Message, MessageType, OpCode, OptionCode};
use crate::stash::Stash;
use byteorder::{BigEndian, ByteOrder};
use failure::{Error, ResultExt};
use fidl_fuchsia_hardware_ethernet_ext::MacAddress as MacAddr;
use serde_derive::{Deserialize, Serialize};
use std::cmp;
use std::collections::{BTreeSet, HashMap, HashSet};
use std::net::Ipv4Addr;
use std::ops::Fn;

/// A minimal DHCP server.
///
/// This comment will be expanded upon in future CLs as the server design
/// is iterated upon.
pub struct Server<F>
where
    F: Fn() -> i64,
{
    cache: CachedClients,
    pool: AddressPool,
    config: ServerConfig,
    time_provider: F,
    stash: Stash,
}

/// The default string used by the Server to identify itself to the Stash service.
pub const DEFAULT_STASH_ID: &str = "dhcpd";
/// The default prefix used by the Server in the keys for values stored in the Stash service.
pub const DEFAULT_STASH_PREFIX: &str = "";

impl<F> Server<F>
where
    F: Fn() -> i64,
{
    /// Returns an initialized `Server` value.
    pub async fn new(time_provider: F) -> Result<Server<F>, Error> {
        Server::from_config(
            ServerConfig::new(),
            time_provider,
            DEFAULT_STASH_ID,
            DEFAULT_STASH_PREFIX,
        )
        .await
    }

    /// Instantiates a server with a random stash identifier.
    /// Used in tests to ensure that each test has an isolated stash instance.
    #[cfg(test)]
    pub async fn new_test_server(time_provider: F) -> Result<Server<F>, Error> {
        use rand::Rng;
        let rand_string: String =
            rand::thread_rng().sample_iter(&rand::distributions::Alphanumeric).take(8).collect();
        Server::from_config(ServerConfig::new(), time_provider, &rand_string, DEFAULT_STASH_PREFIX)
            .await
    }

    /// Instantiates a `Server` value from the provided `ServerConfig`.
    pub async fn from_config<'a>(
        config: ServerConfig,
        time_provider: F,
        stash_id: &'a str,
        stash_prefix: &'a str,
    ) -> Result<Server<F>, Error> {
        let stash = Stash::new(stash_id, stash_prefix).context("failed to instantiate stash")?;
        let cache = stash.load().await?;
        let mut server = Server { cache, pool: AddressPool::new(), config, time_provider, stash };
        server.pool.load_pool(&server.config.managed_addrs);
        Ok(server)
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
            Some(MessageType::DHCPOFFER) => None,
            Some(MessageType::DHCPREQUEST) => self.handle_request(msg),
            Some(MessageType::DHCPDECLINE) => self.handle_decline(msg),
            Some(MessageType::DHCPACK) => None,
            Some(MessageType::DHCPNAK) => None,
            Some(MessageType::DHCPRELEASE) => self.handle_release(msg),
            Some(MessageType::DHCPINFORM) => self.handle_inform(msg),
            None => None,
        }
    }

    fn handle_discover(&mut self, disc: Message) -> Option<Message> {
        let client_config = self.client_config(&disc);
        let offered_ip = self.get_addr(&disc)?;
        let mut offer = build_offer(disc, &self.config, &client_config);
        offer.yiaddr = offered_ip;
        match self.store_client_config(
            Ipv4Addr::from(offer.yiaddr),
            offer.chaddr,
            vec![],
            &client_config,
        ) {
            Ok(()) => Some(offer),
            Err(e) => {
                log::warn!("failed to store client config: {}", e);
                None
            }
        }
    }

    fn get_addr(&mut self, client: &Message) -> Option<Ipv4Addr> {
        if let Some(config) = self.cache.get(&client.chaddr) {
            if !config.expired((self.time_provider)()) {
                // Free cached address so that it can be reallocated to same client.
                self.pool.free_addr(config.client_addr);
                return Some(config.client_addr);
            } else if self.pool.addr_is_available(config.client_addr) {
                return Some(config.client_addr);
            }
        }
        if let Some(opt) = client.get_config_option(OptionCode::RequestedIpAddr) {
            if opt.value.len() >= 4 {
                let requested_addr = protocol::ip_addr_from_buf_at(&opt.value, 0)
                    .expect("out of range indexing on opt.value");
                if self.pool.addr_is_available(requested_addr) {
                    return Some(requested_addr);
                }
            }
        }
        self.pool.get_next_available_addr()
    }

    fn store_client_config(
        &mut self,
        client_addr: Ipv4Addr,
        client_mac: MacAddr,
        client_opts: Vec<ConfigOption>,
        client_config: &ClientConfig,
    ) -> Result<(), Error> {
        let config = CachedConfig {
            client_addr,
            options: client_opts,
            expiration: (self.time_provider)() + client_config.lease_time_s as i64,
        };
        self.stash.store(&client_mac, &config).context("failed to store client in stash")?;
        self.cache.insert(client_mac, config);
        self.pool.allocate_addr(client_addr);
        Ok(())
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
        if !is_recipient(self.config.server_ip, &req) || !self.is_assigned(&req, requested_ip) {
            return None;
        }
        Some(build_ack(req, requested_ip, &self.config))
    }

    fn is_assigned(&self, req: &Message, requested_ip: Ipv4Addr) -> bool {
        if let Some(client_config) = self.cache.get(&req.chaddr) {
            client_config.client_addr == requested_ip
                && !client_config.expired((self.time_provider)())
                && self.pool.addr_is_allocated(requested_ip)
        } else {
            false
        }
    }

    fn handle_request_init_reboot(&mut self, req: Message) -> Option<Message> {
        let requested_ip = get_requested_ip_addr(&req)?;
        if !is_in_subnet(requested_ip, &self.config) {
            return Some(build_nak(req, &self.config));
        }
        if !is_client_mac_known(req.chaddr, &self.cache) {
            return None;
        }
        if !self.is_assigned(&req, requested_ip) {
            return Some(build_nak(req, &self.config));
        }
        Some(build_ack(req, requested_ip, &self.config))
    }

    fn handle_request_renewing(&mut self, req: Message) -> Option<Message> {
        let client_ip = req.ciaddr;
        if !self.is_assigned(&req, client_ip) {
            return None;
        }
        Some(build_ack(req, client_ip, &self.config))
    }

    fn handle_decline(&mut self, dec: Message) -> Option<Message> {
        let declined_ip = get_requested_ip_addr(&dec)?;
        if is_recipient(self.config.server_ip, &dec) && !self.is_assigned(&dec, declined_ip) {
            self.pool.allocate_addr(declined_ip);
        }
        self.cache.remove(&dec.chaddr);
        None
    }

    fn handle_release(&mut self, rel: Message) -> Option<Message> {
        if self.cache.contains_key(&rel.chaddr) {
            self.pool.free_addr(rel.ciaddr);
        }
        None
    }

    fn handle_inform(&mut self, inf: Message) -> Option<Message> {
        // When responding to an INFORM, the server must leave yiaddr zeroed.
        let yiaddr = Ipv4Addr::new(0, 0, 0, 0);
        let mut ack = build_ack(inf, yiaddr, &self.config);
        ack.options.clear();
        add_inform_ack_options(&mut ack, &self.config);
        Some(ack)
    }

    /// Releases all allocated IP addresses whose leases have expired back to
    /// the pool of addresses available for allocation.
    pub fn release_expired_leases(&mut self) {
        let now = (self.time_provider)();
        let expired_clients: Vec<(MacAddr, Ipv4Addr)> = self
            .cache
            .iter()
            .filter(|(_mac, config)| config.expired(now))
            .map(|(mac, config)| (*mac, config.client_addr))
            .collect();
        // Expired client entries must be removed in a separate statement because otherwise we
        // would be attempting to change a cache as we iterate over it.
        for (mac, ip) in expired_clients.iter() {
            self.pool.free_addr(*ip);
            self.cache.remove(mac);
            // The call to delete will immediately be committed to the Stash. Since DHCP lease
            // acquisitions occur on a human timescale, e.g. a cellphone is brought in range of an
            // AP, and at a time resolution of a second, it will be rare for expired_clients to
            // contain sufficient numbers of entries that committing with each deletion will impact
            // performance.
            if let Err(e) = self.stash.delete(&mac) {
                // We log the failed deletion here because it would be the action taken by the
                // caller and we do not want to stop the deletion loop on account of a single
                // failure.
                log::warn!("stash failed to delete client={}: {}", mac, e)
            }
        }
    }

    pub fn client_config(&self, client_message: &Message) -> ClientConfig {
        let requested_config = client_message.parse_to_config();
        ClientConfig {
            lease_time_s: match requested_config.lease_time_s {
                None => self.config.default_lease_time,
                Some(t) => cmp::min(t, self.config.max_lease_time_s),
            },
        }
    }
}

/// Clears the stash instance at the end of a test.
///
/// This implementation is solely for unit testing, where we do not want data stored in
/// the stash to persist past the execution of the test.
#[cfg(test)]
impl<F> Drop for Server<F>
where
    F: Fn() -> i64,
{
    fn drop(&mut self) {
        if !cfg!(test) {
            panic!("dhcp::server::Server implements std::ops::Drop in a non-test cfg");
        }
        let _result = self.stash.clear();
    }
}

/// A cache mapping clients to their configuration data.
///
/// The server should store configuration data for all clients
/// to which it has sent a DHCPOFFER message. Entries in the cache
/// will eventually timeout, although such functionality is currently
/// unimplemented.
pub type CachedClients = HashMap<MacAddr, CachedConfig>;

/// A representation of a DHCP client's stored configuration settings.
///
/// A client's `MacAddr` maps to the `CachedConfig`: this mapping
/// is stored in the `Server`s `CachedClients` instance at runtime, and in
/// `fuchsia.stash` persistent storage.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct CachedConfig {
    client_addr: Ipv4Addr,
    options: Vec<ConfigOption>,
    expiration: i64,
}

impl Default for CachedConfig {
    fn default() -> Self {
        CachedConfig {
            client_addr: Ipv4Addr::new(0, 0, 0, 0),
            options: vec![],
            expiration: std::i64::MAX,
        }
    }
}

impl CachedConfig {
    fn expired(&self, now: i64) -> bool {
        self.expiration <= now
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
        AddressPool { available_addrs: BTreeSet::new(), allocated_addrs: HashSet::new() }
    }

    fn load_pool(&mut self, addrs: &[Ipv4Addr]) {
        for addr in addrs {
            if !self.allocated_addrs.contains(&addr) {
                self.available_addrs.insert(*addr);
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
        if self.available_addrs.remove(&addr) {
            self.allocated_addrs.insert(addr);
        } else {
            panic!("Invalid Server State: attempted to allocate unavailable address");
        }
    }

    fn free_addr(&mut self, addr: Ipv4Addr) {
        if self.allocated_addrs.remove(&addr) {
            self.available_addrs.insert(addr);
        } else {
            panic!("Invalid Server State: attempted to free unallocated address");
        }
    }

    fn addr_is_available(&self, addr: Ipv4Addr) -> bool {
        self.available_addrs.contains(&addr) && !self.allocated_addrs.contains(&addr)
    }

    fn addr_is_allocated(&self, addr: Ipv4Addr) -> bool {
        !self.available_addrs.contains(&addr) && self.allocated_addrs.contains(&addr)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ClientState {
    Unknown,
    Selecting,
    InitReboot,
    Renewing,
}

fn build_offer(client: Message, config: &ServerConfig, client_config: &ClientConfig) -> Message {
    let mut offer = client;
    offer.op = OpCode::BOOTREPLY;
    offer.secs = 0;
    offer.ciaddr = Ipv4Addr::new(0, 0, 0, 0);
    offer.siaddr = Ipv4Addr::new(0, 0, 0, 0);
    offer.sname = String::new();
    offer.file = String::new();
    add_required_options(&mut offer, config, client_config, MessageType::DHCPOFFER);
    add_recommended_options(&mut offer, config);

    offer
}

fn add_required_options(
    msg: &mut Message,
    config: &ServerConfig,
    client_config: &ClientConfig,
    msg_type: MessageType,
) {
    msg.options.clear();
    let mut lease = vec![0; 4];
    BigEndian::write_u32(&mut lease, client_config.lease_time_s);
    msg.options.push(ConfigOption { code: OptionCode::IpAddrLeaseTime, value: lease });
    msg.options.push(ConfigOption {
        code: OptionCode::SubnetMask,
        value: config.subnet_mask.octets().to_vec(),
    });
    msg.options
        .push(ConfigOption { code: OptionCode::DhcpMessageType, value: vec![msg_type.into()] });
    msg.options.push(ConfigOption {
        code: OptionCode::ServerId,
        value: config.server_ip.octets().to_vec(),
    });
}

fn add_recommended_options(msg: &mut Message, config: &ServerConfig) {
    msg.options
        .push(ConfigOption { code: OptionCode::Router, value: ip_vec_to_bytes(&config.routers) });
    msg.options.push(ConfigOption {
        code: OptionCode::NameServer,
        value: ip_vec_to_bytes(&config.name_servers),
    });
    let mut renewal_time = vec![0, 0, 0, 0];
    BigEndian::write_u32(&mut renewal_time, config.default_lease_time / 2);
    msg.options.push(ConfigOption { code: OptionCode::RenewalTime, value: renewal_time });
    let mut rebinding_time = vec![0, 0, 0, 0];
    BigEndian::write_u32(&mut rebinding_time, config.default_lease_time / 4);
    msg.options.push(ConfigOption { code: OptionCode::RebindingTime, value: rebinding_time });
}

fn add_inform_ack_options(msg: &mut Message, config: &ServerConfig) {
    msg.options.push(ConfigOption {
        code: OptionCode::DhcpMessageType,
        value: vec![MessageType::DHCPINFORM.into()],
    });
    msg.options.push(ConfigOption {
        code: OptionCode::ServerId,
        value: config.server_ip.octets().to_vec(),
    });
    msg.options
        .push(ConfigOption { code: OptionCode::Router, value: ip_vec_to_bytes(&config.routers) });
    msg.options.push(ConfigOption {
        code: OptionCode::NameServer,
        value: ip_vec_to_bytes(&config.name_servers),
    });
}

fn ip_vec_to_bytes<'a, T>(ips: T) -> Vec<u8>
where
    T: IntoIterator<Item = &'a Ipv4Addr>,
{
    ips.into_iter().flat_map(|ip| ip.octets().to_vec()).collect()
}

fn is_recipient(server_ip: Ipv4Addr, req: &Message) -> bool {
    if let Some(server_id) = get_server_id_from(&req) {
        return server_id == server_ip;
    }
    false
}

fn build_ack(req: Message, requested_ip: Ipv4Addr, config: &ServerConfig) -> Message {
    let mut ack = req;
    ack.op = OpCode::BOOTREPLY;
    ack.secs = 0;
    ack.yiaddr = requested_ip;
    ack.options.clear();
    add_required_options(
        &mut ack,
        config,
        &ClientConfig::new(config.default_lease_time),
        MessageType::DHCPACK,
    );
    add_recommended_options(&mut ack, config);

    ack
}

fn is_in_subnet(ip: Ipv4Addr, config: &ServerConfig) -> bool {
    config.subnet_mask.apply_to(ip) == config.subnet_mask.apply_to(config.server_ip)
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
        value: vec![MessageType::DHCPNAK.into()],
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

    if maybe_server_id.is_some() && maybe_requested_ip.is_none() && msg.ciaddr != zero_ciaddr {
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
    let req_ip_opt = req.options.iter().find(|opt| opt.code == OptionCode::RequestedIpAddr)?;
    let raw_ip = BigEndian::read_u32(&req_ip_opt.value);
    Some(Ipv4Addr::from(raw_ip))
}

fn get_server_id_from(req: &Message) -> Option<Ipv4Addr> {
    let server_id_opt = req.options.iter().find(|opt| opt.code == OptionCode::ServerId)?;
    let raw_server_id = BigEndian::read_u32(&server_id_opt.value);
    Some(Ipv4Addr::from(raw_server_id))
}

#[cfg(test)]
pub mod tests {

    use super::*;
    use crate::configuration::SubnetMask;
    use crate::protocol::{ConfigOption, Message, MessageType, OpCode, OptionCode};
    use rand::Rng;
    use std::convert::TryFrom;
    use std::net::Ipv4Addr;

    pub fn random_ipv4_generator() -> Ipv4Addr {
        let octet1: u8 = rand::thread_rng().gen();
        let octet2: u8 = rand::thread_rng().gen();
        let octet3: u8 = rand::thread_rng().gen();
        let octet4: u8 = rand::thread_rng().gen();
        Ipv4Addr::new(octet1, octet2, octet3, octet4)
    }

    pub fn random_mac_generator() -> MacAddr {
        let octet1: u8 = rand::thread_rng().gen();
        let octet2: u8 = rand::thread_rng().gen();
        let octet3: u8 = rand::thread_rng().gen();
        let octet4: u8 = rand::thread_rng().gen();
        let octet5: u8 = rand::thread_rng().gen();
        let octet6: u8 = rand::thread_rng().gen();
        MacAddr { octets: [octet1, octet2, octet3, octet4, octet5, octet6] }
    }

    fn server_time_provider() -> i64 {
        42
    }

    async fn new_test_minimal_server<F>(time_provider: F) -> Result<Server<F>, Error>
    where
        F: Fn() -> i64,
    {
        let mut server =
            Server::new_test_server(time_provider).await.context("failed to instantiate server")?;

        server.config.server_ip = random_ipv4_generator();
        server.config.default_lease_time = 100;
        server.config.routers.push(random_ipv4_generator());
        server
            .config
            .name_servers
            .extend_from_slice(&vec![Ipv4Addr::new(8, 8, 8, 8), Ipv4Addr::new(8, 8, 4, 4)]);
        Ok(server)
    }

    fn new_test_discover() -> Message {
        let mut disc = Message::new();
        disc.xid = rand::thread_rng().gen();
        disc.chaddr = random_mac_generator();
        disc.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPDISCOVER.into()],
        });
        disc
    }

    // Creating a new offer needs a reference to `discover` and `server`
    // so it can copy over the essential randomly generated options.
    fn new_test_offer<F>(disc: &Message, server: &Server<F>) -> Message
    where
        F: Fn() -> i64,
    {
        let mut offer = Message::new();
        offer.op = OpCode::BOOTREPLY;
        offer.xid = disc.xid;
        offer.chaddr = disc.chaddr;
        offer
            .options
            .push(ConfigOption { code: OptionCode::IpAddrLeaseTime, value: vec![0, 0, 0, 100] });
        offer.options.push(ConfigOption {
            code: OptionCode::SubnetMask,
            value: SubnetMask::try_from(24).unwrap().octets().to_vec(),
        });
        offer.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPOFFER.into()],
        });
        offer.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: server.config.server_ip.octets().to_vec(),
        });
        offer.options.push(ConfigOption {
            code: OptionCode::Router,
            value: server.config.routers.first().unwrap().octets().to_vec(),
        });
        offer.options.push(ConfigOption {
            code: OptionCode::NameServer,
            value: vec![8, 8, 8, 8, 8, 8, 4, 4],
        });
        offer
            .options
            .push(ConfigOption { code: OptionCode::RenewalTime, value: vec![0, 0, 0, 50] });
        offer
            .options
            .push(ConfigOption { code: OptionCode::RebindingTime, value: vec![0, 0, 0, 25] });
        offer
    }

    fn new_test_request() -> Message {
        let mut req = Message::new();
        req.xid = rand::thread_rng().gen();
        req.chaddr = random_mac_generator();
        req.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPREQUEST.into()],
        });
        req
    }

    fn new_test_request_selecting_state<F>(server: &Server<F>) -> Message
    where
        F: Fn() -> i64,
    {
        let mut req = new_test_request();
        req.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: server.config.server_ip.octets().to_vec(),
        });
        req
    }

    fn new_test_ack<F>(req: &Message, server: &Server<F>) -> Message
    where
        F: Fn() -> i64,
    {
        let mut ack = Message::new();
        ack.op = OpCode::BOOTREPLY;
        ack.xid = req.xid;
        ack.chaddr = req.chaddr;
        ack.options
            .push(ConfigOption { code: OptionCode::IpAddrLeaseTime, value: vec![0, 0, 0, 100] });
        ack.options.push(ConfigOption {
            code: OptionCode::SubnetMask,
            value: SubnetMask::try_from(24).unwrap().octets().to_vec(),
        });
        ack.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPACK.into()],
        });
        ack.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: server.config.server_ip.octets().to_vec(),
        });
        ack.options.push(ConfigOption {
            code: OptionCode::Router,
            value: server.config.routers.first().unwrap().octets().to_vec(),
        });
        ack.options.push(ConfigOption {
            code: OptionCode::NameServer,
            value: vec![8, 8, 8, 8, 8, 8, 4, 4],
        });
        ack.options.push(ConfigOption { code: OptionCode::RenewalTime, value: vec![0, 0, 0, 50] });
        ack.options
            .push(ConfigOption { code: OptionCode::RebindingTime, value: vec![0, 0, 0, 25] });
        ack
    }

    fn new_test_nak<F>(req: &Message, server: &Server<F>) -> Message
    where
        F: Fn() -> i64,
    {
        let mut nak = Message::new();
        nak.op = OpCode::BOOTREPLY;
        nak.xid = req.xid;
        nak.chaddr = req.chaddr;
        nak.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPNAK.into()],
        });
        nak.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: server.config.server_ip.octets().to_vec(),
        });
        nak
    }

    fn new_test_release() -> Message {
        let mut release = Message::new();
        release.xid = rand::thread_rng().gen();
        release.chaddr = random_mac_generator();
        release.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPRELEASE.into()],
        });
        release
    }

    fn new_test_inform() -> Message {
        let mut inform = Message::new();
        inform.xid = rand::thread_rng().gen();
        inform.chaddr = random_mac_generator();
        inform.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPINFORM.into()],
        });
        inform
    }

    fn new_test_inform_ack<F>(req: &Message, server: &Server<F>) -> Message
    where
        F: Fn() -> i64,
    {
        let mut ack = Message::new();
        ack.op = OpCode::BOOTREPLY;
        ack.xid = req.xid;
        ack.chaddr = req.chaddr;
        ack.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPINFORM.into()],
        });
        ack.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: server.config.server_ip.octets().to_vec(),
        });
        ack.options.push(ConfigOption {
            code: OptionCode::Router,
            value: server.config.routers.first().unwrap().octets().to_vec(),
        });
        ack.options.push(ConfigOption {
            code: OptionCode::NameServer,
            value: vec![8, 8, 8, 8, 8, 8, 4, 4],
        });
        ack
    }

    fn new_test_decline<F>(server: &Server<F>) -> Message
    where
        F: Fn() -> i64,
    {
        let mut decline = Message::new();
        decline.xid = rand::thread_rng().gen();
        decline.chaddr = random_mac_generator();
        decline.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPDECLINE.into()],
        });
        decline.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: server.config.server_ip.octets().to_vec(),
        });
        decline
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_returns_correct_offer() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let disc = new_test_discover();

        let offer_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(offer_ip);

        let mut expected_offer = new_test_offer(&disc, &server);
        expected_offer.yiaddr = offer_ip;

        assert_eq!(server.dispatch(disc), Some(expected_offer));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_updates_server_state() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let disc = new_test_discover();

        let offer_ip = random_ipv4_generator();
        let client_mac = disc.chaddr;

        server.pool.available_addrs.insert(offer_ip);

        let expected_client_config = CachedConfig {
            client_addr: offer_ip,
            options: vec![],
            expiration: server_time_provider() + server.config.default_lease_time as i64,
        };

        let _response = server.dispatch(disc);

        assert_eq!(server.pool.available_addrs.len(), 0);
        assert_eq!(server.pool.allocated_addrs.len(), 1);
        assert_eq!(server.cache.len(), 1);
        assert_eq!(server.cache.get(&client_mac), Some(&expected_client_config));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_updates_stash() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let disc = new_test_discover();

        let offer_ip = random_ipv4_generator();
        let client_mac = disc.chaddr;

        server.pool.available_addrs.insert(offer_ip);

        let offer = server.dispatch(disc).unwrap();

        let accessor = server.stash.clone_proxy();
        let maybe_value = accessor
            .get_value(&format!("{}-{}", DEFAULT_STASH_PREFIX, client_mac))
            .await
            .context("failed to get value from stash")?;
        let value = maybe_value.unwrap();
        let serialized_config = match *value {
            fidl_fuchsia_stash::Value::Stringval(s) => s,
            _ => return Err(failure::err_msg("stash did not contain expected value")),
        };
        let deserialized_config = serde_json::from_str::<CachedConfig>(&serialized_config)
            .context("failed to deserialize config")?;

        assert_eq!(deserialized_config.client_addr, offer.yiaddr);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_client_binding_returns_bound_addr() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);

        server.cache.insert(
            disc.chaddr,
            CachedConfig {
                client_addr: bound_client_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(response.yiaddr, bound_client_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_returns_available_old_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(bound_client_ip);

        server.cache.insert(
            disc.chaddr,
            CachedConfig { client_addr: bound_client_ip, options: vec![], expiration: 0 },
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(response.yiaddr, bound_client_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_unavailable_addr_returns_next_free_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();
        let free_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        server.pool.available_addrs.insert(free_ip);

        server.cache.insert(
            disc.chaddr,
            CachedConfig { client_addr: bound_client_ip, options: vec![], expiration: 0 },
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(response.yiaddr, free_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_available_requested_addr_returns_requested_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut disc = new_test_discover();

        let requested_ip = random_ipv4_generator();
        let free_ip_1 = random_ipv4_generator();
        let free_ip_2 = random_ipv4_generator();

        server.pool.available_addrs.insert(free_ip_1);
        server.pool.available_addrs.insert(requested_ip);
        server.pool.available_addrs.insert(free_ip_2);

        // Update discover message to request for a specific ip
        // which is available in server pool.
        disc.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: requested_ip.octets().to_vec(),
        });

        let response = server.dispatch(disc).unwrap();

        assert_eq!(response.yiaddr, requested_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_unavailable_requested_addr_returns_next_free_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut disc = new_test_discover();

        let requested_ip = Ipv4Addr::new(192, 1, 1, 5);
        let free_ip_1 = Ipv4Addr::new(192, 168, 10, 24);
        let free_ip_2 = Ipv4Addr::new(192, 168, 20, 72);

        server.pool.allocated_addrs.insert(requested_ip);
        server.pool.available_addrs.insert(free_ip_1);
        server.pool.available_addrs.insert(free_ip_2);

        // Update discover message to request for a specific ip
        // which is not available in server pool.
        disc.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: requested_ip.octets().to_vec(),
        });

        let response = server.dispatch(disc).unwrap();

        assert_eq!(response.yiaddr, free_ip_1);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_returns_correct_ack() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let offered_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(offered_ip);

        // Update message to request for ip previously offered by server.
        req.ciaddr = offered_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig { client_addr: offered_ip, options: vec![], expiration: std::i64::MAX },
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.ciaddr = offered_ip;
        expected_ack.yiaddr = offered_ip;

        assert_eq!(server.dispatch(req), Some(expected_ack));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_no_address_allocation_to_client_returns_none(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        // Update message to request for ip which is unknown by server.
        req.ciaddr = random_ipv4_generator();

        assert_eq!(server.dispatch(req), None);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_wrong_server_ip_returns_none() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        // Update message to request for any ip.
        req.ciaddr = random_ipv4_generator();

        // Update request to have a server ip different from actual server ip.
        req.options.remove(1);
        req.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: random_ipv4_generator().octets().to_vec(),
        });

        assert_eq!(server.dispatch(req), None);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_maintains_server_invariants() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let offered_ip = random_ipv4_generator();
        let client_mac = req.chaddr;

        server.pool.allocated_addrs.insert(offered_ip);

        req.ciaddr = offered_ip;

        server.cache.insert(
            client_mac,
            CachedConfig { client_addr: offered_ip, options: vec![], expiration: std::i64::MAX },
        );

        let _ack: Message = server.dispatch(req).unwrap();

        assert!(server.cache.contains_key(&client_mac));
        assert!(server.pool.addr_is_allocated(offered_ip));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_no_client_cache_maintains_server_invariants(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let offered_ip = random_ipv4_generator();
        let client_mac = req.chaddr;

        req.ciaddr = offered_ip;

        assert_eq!(server.dispatch(req), None);
        assert!(!server.cache.contains_key(&client_mac));
        assert!(!server.pool.addr_is_allocated(offered_ip));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_returns_correct_ack() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        // For init-reboot, server and requested ip must be on the same subnet.
        // Hard-coding ip values here to achieve that.
        let init_reboot_client_ip = Ipv4Addr::new(192, 168, 1, 60);
        server.config.server_ip = Ipv4Addr::new(192, 168, 1, 1);

        server.pool.allocated_addrs.insert(init_reboot_client_ip);

        // Update request to have the test requested ip.
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: init_reboot_client_ip.octets().to_vec(),
        });

        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: init_reboot_client_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.yiaddr = init_reboot_client_ip;

        assert_eq!(server.dispatch(req), Some(expected_ack));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_client_on_wrong_subnet_returns_nak(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        // Update request to have requested ip not on same subnet as server.
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: random_ipv4_generator().octets().to_vec(),
        });

        // The returned nak should be from this recipient server.
        let expected_nak = new_test_nak(&req, &server);

        assert_eq!(server.dispatch(req), Some(expected_nak));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_unknown_client_mac_returns_none(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        // Update requested ip and server ip to be on the same subnet.
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: Ipv4Addr::new(192, 165, 30, 45).octets().to_vec(),
        });
        server.config.server_ip = Ipv4Addr::new(192, 165, 30, 1);

        assert_eq!(server.dispatch(req), None);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_invalid_client_binding_returns_nak(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        // Update requested ip and server ip to be on the same subnet.
        let init_reboot_client_ip = Ipv4Addr::new(192, 165, 25, 4);
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: init_reboot_client_ip.octets().to_vec(),
        });
        server.config.server_ip = Ipv4Addr::new(192, 165, 25, 1);

        server.pool.allocated_addrs.insert(init_reboot_client_ip);

        // Expire client binding to make it invalid.
        server.cache.insert(
            req.chaddr,
            CachedConfig { client_addr: init_reboot_client_ip, options: vec![], expiration: 0 },
        );

        let expected_nak = new_test_nak(&req, &server);

        assert_eq!(server.dispatch(req), Some(expected_nak));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_returns_correct_ack() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        req.ciaddr = bound_client_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: bound_client_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.yiaddr = bound_client_ip;
        expected_ack.ciaddr = bound_client_ip;

        assert_eq!(server.dispatch(req), Some(expected_ack));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_unknown_client_returns_none() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        req.ciaddr = random_ipv4_generator();

        assert_eq!(server.dispatch(req), None);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_client_state_with_selecting_returns_selecting() -> Result<(), Error> {
        let mut req = new_test_request();

        // Selecting state request must have server id and ciaddr populated.
        req.ciaddr = random_ipv4_generator();
        req.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: random_ipv4_generator().octets().to_vec(),
        });

        assert_eq!(get_client_state(&req), ClientState::Selecting);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_client_state_with_initreboot_returns_initreboot() -> Result<(), Error> {
        let mut req = new_test_request();

        // Init reboot state request must have requested ip populated.
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: random_ipv4_generator().octets().to_vec(),
        });

        assert_eq!(get_client_state(&req), ClientState::InitReboot);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_client_state_with_renewing_returns_renewing() -> Result<(), Error> {
        let mut req = new_test_request();

        // Renewing state request must have ciaddr populated.
        req.ciaddr = random_ipv4_generator();

        assert_eq!(get_client_state(&req), ClientState::Renewing);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_client_state_with_unknown_returns_unknown() -> Result<(), Error> {
        let msg = new_test_request();

        assert_eq!(get_client_state(&msg), ClientState::Unknown);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_release_expired_leases_with_none_expired_releases_none() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        server.pool.available_addrs.clear();

        // Insert client 1 bindings.
        let client_1_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_1_ip);
        server.store_client_config(
            client_1_ip,
            random_mac_generator(),
            vec![],
            &ClientConfig { lease_time_s: std::u32::MAX },
        )?;

        // Insert client 2 bindings.
        let client_2_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_2_ip);
        server.store_client_config(
            client_2_ip,
            random_mac_generator(),
            vec![],
            &ClientConfig { lease_time_s: std::u32::MAX },
        )?;

        // Insert client 3 bindings.
        let client_3_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_3_ip);
        server.store_client_config(
            client_3_ip,
            random_mac_generator(),
            vec![],
            &ClientConfig { lease_time_s: std::u32::MAX },
        )?;

        server.release_expired_leases();

        assert_eq!(server.cache.len(), 3);
        assert_eq!(server.pool.available_addrs.len(), 0);
        assert_eq!(server.pool.allocated_addrs.len(), 3);
        let keys = get_keys(&mut server).await.context("failed to get keys")?;
        assert_eq!(keys.len(), 3);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_release_expired_leases_with_all_expired_releases_all() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        server.pool.available_addrs.clear();

        // Insert client 1 bindings.
        let client_1_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_1_ip);
        let () = server.store_client_config(
            client_1_ip,
            random_mac_generator(),
            vec![],
            &ClientConfig { lease_time_s: 0 },
        )?;

        // Insert client 2 bindings.
        let client_2_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_2_ip);
        let () = server.store_client_config(
            client_2_ip,
            random_mac_generator(),
            vec![],
            &ClientConfig { lease_time_s: 0 },
        )?;

        // Insert client 3 bindings.
        let client_3_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_3_ip);
        let () = server.store_client_config(
            client_3_ip,
            random_mac_generator(),
            vec![],
            &ClientConfig { lease_time_s: 0 },
        )?;

        server.release_expired_leases();

        assert_eq!(server.cache.len(), 0);
        assert_eq!(server.pool.available_addrs.len(), 3);
        assert_eq!(server.pool.allocated_addrs.len(), 0);
        let keys = get_keys(&mut server).await.context("failed to get keys")?;
        assert_eq!(keys.len(), 0);
        Ok(())
    }

    async fn get_keys<F>(server: &mut Server<F>) -> Result<Vec<fidl_fuchsia_stash::KeyValue>, Error>
    where
        F: Fn() -> i64,
    {
        let accessor = server.stash.clone_proxy();
        let (iter, server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_stash::GetIteratorMarker>()
                .context("failed to create iterator")?;
        let () = accessor
            .get_prefix(&format!("{}", DEFAULT_STASH_PREFIX), server_end)
            .context("failed to get prefix")?;
        let keys = iter.get_next().await.context("failed to get next")?;
        Ok(keys)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_release_expired_leases_with_some_expired_releases_expired() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        server.pool.available_addrs.clear();

        // Insert client 1 bindings.
        let client_1_mac = random_mac_generator();
        let client_1_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_1_ip);
        let () = server.store_client_config(
            client_1_ip,
            client_1_mac,
            vec![],
            &ClientConfig { lease_time_s: std::u32::MAX },
        )?;

        // Insert client 2 bindings.
        let client_2_mac = random_mac_generator();
        let client_2_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_2_ip);
        let () = server.store_client_config(
            client_2_ip,
            client_2_mac,
            vec![],
            &ClientConfig { lease_time_s: 0 },
        )?;

        // Insert client 3 bindings.
        let client_3_mac = random_mac_generator();
        let client_3_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_3_ip);
        let () = server.store_client_config(
            client_3_ip,
            client_3_mac,
            vec![],
            &ClientConfig { lease_time_s: std::u32::MAX },
        )?;

        server.release_expired_leases();

        assert_eq!(server.cache.len(), 2);
        assert!(!server.cache.contains_key(&client_2_mac));
        assert_eq!(server.pool.available_addrs.len(), 1);
        assert_eq!(server.pool.allocated_addrs.len(), 2);
        let keys = get_keys(&mut server).await.context("failed to get keys")?;
        assert_eq!(keys.len(), 2);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_known_release_updates_address_pool_retains_client_config(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut release = new_test_release();

        let release_ip = random_ipv4_generator();
        let client_mac = release.chaddr;

        server.pool.allocated_addrs.insert(release_ip);
        release.ciaddr = release_ip;

        let test_client_config =
            CachedConfig { client_addr: release_ip, options: vec![], expiration: std::i64::MAX };

        server.cache.insert(client_mac, test_client_config.clone());

        // Server must not send response for `DHCPRELEASE` messages.
        assert_eq!(server.dispatch(release), None);

        assert!(!server.pool.addr_is_allocated(release_ip), "addr marked allocated");
        assert!(server.pool.addr_is_available(release_ip), "addr not marked available");
        assert!(server.cache.contains_key(&client_mac), "client config not retained");
        assert_eq!(
            server.cache.get(&client_mac).unwrap(),
            &test_client_config,
            "retained client config changed"
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_unknown_release_maintains_server_state() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut release = new_test_release();

        let release_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(release_ip);
        release.ciaddr = release_ip;

        // Server must not send response for `DHCPRELEASE` messages.
        assert_eq!(server.dispatch(release), None);

        assert!(server.pool.addr_is_allocated(release_ip), "addr not marked allocated");
        assert!(!server.pool.addr_is_available(release_ip), "addr still marked available");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_inform_returns_correct_ack() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut inform = new_test_inform();

        let inform_client_ip = random_ipv4_generator();

        inform.ciaddr = inform_client_ip;

        let mut expected_ack = new_test_inform_ack(&inform, &server);
        expected_ack.ciaddr = inform_client_ip;

        assert_eq!(server.dispatch(inform), Some(expected_ack));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_decline_for_valid_client_binding_updates_cache() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: declined_ip.octets().to_vec(),
        });

        server.pool.allocated_addrs.insert(declined_ip);

        server.cache.insert(
            client_mac,
            CachedConfig { client_addr: declined_ip, options: vec![], expiration: std::i64::MAX },
        );

        // Server must not send response for `DHCPDECLINE` messages.
        assert_eq!(server.dispatch(decline), None);

        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.cache.contains_key(&client_mac), "client config incorrectly retained");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_decline_for_invalid_client_binding_updates_pool_and_cache(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: declined_ip.octets().to_vec(),
        });

        // Even though declined client ip does not match client binding,
        // the server must update its address pool and mark declined ip as
        // allocated, and delete client bindings from its cache.
        let client_ip_according_to_server = random_ipv4_generator();

        server.pool.allocated_addrs.insert(client_ip_according_to_server);
        server.pool.available_addrs.insert(declined_ip);

        // Server contains client bindings which reflect a different address
        // than the one being declined.
        server.cache.insert(
            client_mac,
            CachedConfig {
                client_addr: client_ip_according_to_server,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        // Server must not send response for `DHCPDECLINE` messages.
        assert_eq!(server.dispatch(decline), None);

        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.cache.contains_key(&client_mac), "client config incorrectly retained");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_decline_for_expired_client_binding_updates_pool_and_cache(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: declined_ip.octets().to_vec(),
        });

        server.pool.available_addrs.insert(declined_ip);

        server.cache.insert(
            client_mac,
            CachedConfig { client_addr: declined_ip, options: vec![], expiration: 0 },
        );

        // Server must not send response for `DHCPDECLINE` messages.
        assert_eq!(server.dispatch(decline), None);

        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.cache.contains_key(&client_mac), "client config incorrectly retained");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic]
    //TODO(NET_2445) Revisit when decline behavior is verified.
    // Currently if a decline is sent by a known client for an address which is
    // not available in server pool, the code panics.
    async fn test_dispatch_with_decline_known_client_for_address_not_in_server_pool_panics() {
        let mut server = match new_test_minimal_server(server_time_provider).await {
            Ok(s) => s,
            Err(e) => panic!("{}", e),
        };
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: declined_ip.octets().to_vec(),
        });

        // Server contains client bindings which reflect a different address
        // than the one being declined.
        server.cache.insert(
            client_mac,
            CachedConfig {
                client_addr: random_ipv4_generator(),
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        server.dispatch(decline);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_decline_for_unknown_client_updates_pool() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();

        decline.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: declined_ip.octets().to_vec(),
        });

        server.pool.available_addrs.insert(declined_ip);

        // Server must not send a response for `DHCPDECLINE` messages.
        assert_eq!(server.dispatch(decline), None);

        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    //TODO(NET_2445) Revisit when decline behavior is verified.
    async fn test_dispatch_with_decline_for_incorrect_server_recepient_deletes_client_binding(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        server.config.server_ip = Ipv4Addr::new(192, 168, 1, 1);

        let mut decline = new_test_decline(&server);

        // Updating decline request to have wrong server ip.
        decline.options.remove(1);
        decline.options.push(ConfigOption {
            code: OptionCode::ServerId,
            value: Ipv4Addr::new(1, 2, 3, 4).octets().to_vec(),
        });

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: declined_ip.octets().to_vec(),
        });

        server.pool.allocated_addrs.insert(declined_ip);
        server.cache.insert(
            client_mac,
            CachedConfig { client_addr: declined_ip, options: vec![], expiration: std::i64::MAX },
        );

        // Server must not send a response for `DHCPDECLINE` messages.
        assert_eq!(server.dispatch(decline), None);

        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.cache.contains_key(&client_mac), "client config incorrectly retained");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_client_requested_lease_time() -> Result<(), Error> {
        let mut disc = new_test_discover();
        let client_mac = disc.chaddr;

        let client_requested_time: u8 = 20;

        disc.options.push(ConfigOption {
            code: OptionCode::IpAddrLeaseTime,
            value: vec![0, 0, 0, client_requested_time],
        });

        let mut server = new_test_minimal_server(server_time_provider).await?;
        server.pool.available_addrs.insert(random_ipv4_generator());

        let response = server.dispatch(disc).unwrap();
        assert_eq!(
            BigEndian::read_u32(
                &response.get_config_option(OptionCode::IpAddrLeaseTime).unwrap().value
            ),
            client_requested_time as u32
        );

        assert_eq!(
            server.cache.get(&client_mac).unwrap().expiration,
            server_time_provider() + client_requested_time as i64
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_client_requested_lease_time_greater_than_max() -> Result<(), Error> {
        let mut disc = new_test_discover();
        let client_mac = disc.chaddr;

        let client_requested_time: u8 = 20;
        let server_max_lease_time: u32 = 10;

        disc.options.push(ConfigOption {
            code: OptionCode::IpAddrLeaseTime,
            value: vec![0, 0, 0, client_requested_time],
        });

        let mut server = new_test_minimal_server(server_time_provider).await?;
        server.pool.available_addrs.insert(Ipv4Addr::new(195, 168, 1, 45));
        server.config.max_lease_time_s = server_max_lease_time;

        let response = server.dispatch(disc).unwrap();
        assert_eq!(
            BigEndian::read_u32(
                &response.get_config_option(OptionCode::IpAddrLeaseTime).unwrap().value
            ),
            server_max_lease_time
        );

        assert_eq!(
            server.cache.get(&client_mac).unwrap().expiration,
            server_time_provider() + server_max_lease_time as i64
        );
        Ok(())
    }
}
