// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::configuration::{ClientConfig, ServerConfig};
use crate::protocol::{
    self, ConfigOption, Message, MessageType, MessageTypeError, OpCode, OptionCode,
};
use crate::stash::Stash;
use byteorder::{BigEndian, ByteOrder};
use failure::{Error, Fail, ResultExt};
use fidl_fuchsia_hardware_ethernet_ext::MacAddress as MacAddr;
use serde_derive::{Deserialize, Serialize};
use std::cmp;
use std::collections::{BTreeSet, HashMap, HashSet};
use std::fmt;
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

/// This enumerates the actions a DHCP server can take in response to a
/// received client message. A `SendResponse(Message)` indicates that a
/// `Message` needs to be sent back to the client. The other two variants
/// indicate a successful processing of a client `Decline` or `Release`.
/// Implements `PartialEq` for test assertions.
#[derive(Debug, PartialEq)]
pub enum ServerAction {
    SendResponse(Message),
    AddressDecline(Ipv4Addr),
    AddressRelease(Ipv4Addr),
}

/// A wrapper around the error types which can be returned by DHCP Server
/// in response to client requests.
/// Implements `PartialEq` for test assertions.
#[derive(Debug, Fail, PartialEq)]
pub enum ServerError {
    #[fail(display = "unexpected client message type: {}", _0)]
    UnexpectedClientMessageType(MessageType),

    #[fail(display = "requested ip parsing failure: {}", _0)]
    BadRequestedIpv4Addr(String),

    #[fail(display = "local address pool manipulation error: {}", _0)]
    ServerAddressPoolFailure(AddressPoolError),

    #[fail(display = "incorrect server ip in client message: {}", _0)]
    IncorrectDHCPServer(Ipv4Addr),

    #[fail(display = "requested ip mismatch with offered ip: {} {}", _0, _1)]
    RequestedIpOfferIpMismatch(Ipv4Addr, Ipv4Addr),

    #[fail(display = "expired client config")]
    ExpiredClientConfig,

    #[fail(display = "requested ip absent from server pool: {}", _0)]
    UnidentifiedRequestedIp(Ipv4Addr),

    #[fail(display = "unknown client mac: {}", _0)]
    UnknownClientMac(MacAddr),

    #[fail(display = "init reboot request did not include ip")]
    NoRequestedAddrAtInitReboot,

    #[fail(display = "unidentified client state during request")]
    UnknownClientStateDuringRequest,

    #[fail(display = "decline request did not include ip")]
    NoRequestedAddrForDecline,

    #[fail(display = "unknown client message type: {}", _0)]
    UnidentifiedClientMessageType(u8),

    #[fail(display = "client request did not include required message type option")]
    MissingMessageTypeOption,

    #[fail(display = "client request did not include required message type")]
    MissingMessageTypeValue,

    #[fail(display = "error manipulating server cache: {}", _0)]
    ServerCacheUpdateFailure(StashError),
}

impl From<AddressPoolError> for ServerError {
    fn from(e: AddressPoolError) -> Self {
        ServerError::ServerAddressPoolFailure(e)
    }
}

/// This struct is used to hold the `failure::Error` returned by the server's
/// Stash manipulation methods. We manually implement `PartialEq` so this
/// struct could be included in the `ServerError` enum,
/// which are asserted for equality in tests.
#[derive(Debug)]
pub struct StashError {
    error: Error,
}

impl PartialEq for StashError {
    fn eq(&self, _other: &Self) -> bool {
        false
    }
}

impl fmt::Display for StashError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&self, f)
    }
}

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
    /// and return the suitable response.
    /// If the incoming message is invalid, or the server is unable to serve the request,
    /// or the processing of the client's request resulted in an error, then `dispatch()`
    /// will return the fitting `Err` indicating what went wrong.
    pub fn dispatch(&mut self, msg: Message) -> Result<ServerAction, ServerError> {
        match msg.get_dhcp_type() {
            Ok(MessageType::DHCPDISCOVER) => self.handle_discover(msg),
            Ok(MessageType::DHCPOFFER) => {
                Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPOFFER))
            }
            Ok(MessageType::DHCPREQUEST) => self.handle_request(msg),
            Ok(MessageType::DHCPDECLINE) => self.handle_decline(msg),
            Ok(MessageType::DHCPACK) => {
                Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPACK))
            }
            Ok(MessageType::DHCPNAK) => {
                Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPNAK))
            }
            Ok(MessageType::DHCPRELEASE) => self.handle_release(msg),
            Ok(MessageType::DHCPINFORM) => self.handle_inform(msg),
            Err(MessageTypeError::MissingMessageTypeOption) => {
                Err(ServerError::MissingMessageTypeOption)
            }
            Err(MessageTypeError::MissingMessageTypeValue) => {
                Err(ServerError::MissingMessageTypeValue)
            }
            Err(MessageTypeError::UnknownMessageType(typ)) => {
                Err(ServerError::UnidentifiedClientMessageType(typ))
            }
        }
    }

    fn handle_discover(&mut self, disc: Message) -> Result<ServerAction, ServerError> {
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
            Ok(()) => Ok(ServerAction::SendResponse(offer)),
            Err(e) => Err(ServerError::ServerCacheUpdateFailure(StashError { error: e })),
        }
    }

    fn get_addr(&mut self, client: &Message) -> Result<Ipv4Addr, ServerError> {
        if let Some(config) = self.cache.get(&client.chaddr) {
            if !config.expired((self.time_provider)()) {
                // Release cached address so that it can be reallocated to same client.
                // This should NEVER return an `Err`. If it does it indicates
                // the server's notion of client bindings is wrong.
                // Its non-recoverable and we therefore panic.
                if let Err(AddressPoolError::UnallocatedIpv4AddrRelease(addr)) =
                    self.pool.release_addr(config.client_addr)
                {
                    panic!("server tried to release unallocated ip {}", addr)
                }
                return Ok(config.client_addr);
            } else if self.pool.addr_is_available(config.client_addr) {
                return Ok(config.client_addr);
            }
        }
        if let Some(opt) = client.get_config_option(OptionCode::RequestedIpAddr) {
            if opt.value.len() >= 4 {
                let requested_addr = protocol::ip_addr_from_buf_at(&opt.value, 0).ok_or(
                    ServerError::BadRequestedIpv4Addr(
                        "out of range indexing on opt.value".to_owned(),
                    ),
                )?;
                if self.pool.addr_is_available(requested_addr) {
                    return Ok(requested_addr);
                }
            }
        }
        self.pool.get_next_available_addr().map_err(AddressPoolError::into)
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
        // This should NEVER return an `Err`. If it does it indicates
        // server's state has changed in the middle of request handling.
        // This is non-recoverable and we therefore panic.
        if let Err(AddressPoolError::UnavailableIpv4AddrAllocation(addr)) =
            self.pool.allocate_addr(client_addr)
        {
            panic!("server tried to allocate unavailable ip {}", addr)
        }
        Ok(())
    }

    fn handle_request(&mut self, req: Message) -> Result<ServerAction, ServerError> {
        match get_client_state(&req) {
            ClientState::Selecting => self.handle_request_selecting(req),
            ClientState::InitReboot => self.handle_request_init_reboot(req),
            ClientState::Renewing => self.handle_request_renewing(req),
            ClientState::Unknown => Err(ServerError::UnknownClientStateDuringRequest),
        }
    }

    fn handle_request_selecting(&mut self, req: Message) -> Result<ServerAction, ServerError> {
        let requested_ip = req.ciaddr;
        if !is_recipient(self.config.server_ip, &req) {
            Err(ServerError::IncorrectDHCPServer(self.config.server_ip))
        } else {
            let () = self.validate_requested_addr_with_client(&req, requested_ip)?;
            Ok(ServerAction::SendResponse(build_ack(req, requested_ip, &self.config)))
        }
    }

    /// The function below validates if the `requested_ip` is correctly
    /// associated with the client whose request `req` is being processed.
    ///
    /// It first checks if the client bindings can be found in server cache.
    /// If not, the association is wrong and it returns an `Err()`.
    ///
    /// If the server can correctly locate the client bindings in its cache,
    /// it further verifies if the `requested_ip` is the same as the ip address
    /// represented in the bindings and the binding is not expired and that the
    /// `requested_ip` is no longer available in the server address pool. If
    /// all the above conditions are met, it returns an `Ok(())` else the
    /// appropriate `Err()` value is returned.
    fn validate_requested_addr_with_client(
        &self,
        req: &Message,
        requested_ip: Ipv4Addr,
    ) -> Result<(), ServerError> {
        if let Some(client_config) = self.cache.get(&req.chaddr) {
            if client_config.client_addr != requested_ip {
                Err(ServerError::RequestedIpOfferIpMismatch(
                    requested_ip,
                    client_config.client_addr,
                ))
            } else if client_config.expired((self.time_provider)()) {
                Err(ServerError::ExpiredClientConfig)
            } else if !self.pool.addr_is_allocated(requested_ip) {
                Err(ServerError::UnidentifiedRequestedIp(requested_ip))
            } else {
                Ok(())
            }
        } else {
            Err(ServerError::UnknownClientMac(req.chaddr))
        }
    }

    fn handle_request_init_reboot(&mut self, req: Message) -> Result<ServerAction, ServerError> {
        let requested_ip =
            get_requested_ip_addr(&req).ok_or(ServerError::NoRequestedAddrAtInitReboot)?;
        if !is_in_subnet(requested_ip, &self.config) {
            let error_msg = String::from("client and server are in different subnets");
            return Ok(ServerAction::SendResponse(build_nak(req, &self.config, error_msg)));
        }
        if !is_client_mac_known(req.chaddr, &self.cache) {
            return Err(ServerError::UnknownClientMac(req.chaddr));
        }
        if self.validate_requested_addr_with_client(&req, requested_ip).is_err() {
            let error_msg = String::from("requested ip is not assigned to client");
            return Ok(ServerAction::SendResponse(build_nak(req, &self.config, error_msg)));
        }
        Ok(ServerAction::SendResponse(build_ack(req, requested_ip, &self.config)))
    }

    fn handle_request_renewing(&mut self, req: Message) -> Result<ServerAction, ServerError> {
        let client_ip = req.ciaddr;
        self.validate_requested_addr_with_client(&req, client_ip)
            .and(Ok(ServerAction::SendResponse(build_ack(req, client_ip, &self.config))))
    }

    /// TODO(NET-2445) Ensure server behavior is as intended.
    fn handle_decline(&mut self, dec: Message) -> Result<ServerAction, ServerError> {
        let declined_ip =
            get_requested_ip_addr(&dec).ok_or_else(|| ServerError::NoRequestedAddrForDecline)?;
        if is_recipient(self.config.server_ip, &dec)
            && self.validate_requested_addr_with_client(&dec, declined_ip).is_err()
        {
            let () = self.pool.allocate_addr(declined_ip)?;
        }
        self.cache.remove(&dec.chaddr);
        Ok(ServerAction::AddressDecline(declined_ip))
    }

    fn handle_release(&mut self, rel: Message) -> Result<ServerAction, ServerError> {
        if self.cache.contains_key(&rel.chaddr) {
            let () = self.pool.release_addr(rel.ciaddr)?;
            Ok(ServerAction::AddressRelease(rel.ciaddr))
        } else {
            Err(ServerError::UnknownClientMac(rel.chaddr))
        }
    }

    fn handle_inform(&mut self, inf: Message) -> Result<ServerAction, ServerError> {
        // When responding to an INFORM, the server must leave yiaddr zeroed.
        let yiaddr = Ipv4Addr::new(0, 0, 0, 0);
        let mut ack = build_ack(inf, yiaddr, &self.config);
        ack.options.clear();
        add_inform_ack_options(&mut ack, &self.config);
        Ok(ServerAction::SendResponse(ack))
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
            //  We ignore the `Result` here since a failed release of the `ip`
            // in this iteration will be reattempted in the next.
            let _release_result = self.pool.release_addr(*ip);
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

//This is a wrapper around different errors that could be returned by
// the DHCP server address pool during address allocation/de-allocation.
#[derive(Debug, Fail, PartialEq)]
pub enum AddressPoolError {
    #[fail(display = "address pool does not have any available ip to hand out")]
    Ipv4AddrExhaustion,

    #[fail(display = "attempted to allocate unavailable ip: {}", _0)]
    UnavailableIpv4AddrAllocation(Ipv4Addr),

    #[fail(display = " attempted to release unallocated ip: {}", _0)]
    UnallocatedIpv4AddrRelease(Ipv4Addr),
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

    /// TODO(NET-2446): The ip should be handed out based on client subnet
    /// Currently, the server blindly hands out the next available ip
    /// from its available ip pool, without any subnet analysis.
    ///
    /// RFC2131#section-4.3.1
    ///
    /// A new address allocated from the server's pool of available
    /// addresses; the address is selected based on the subnet from which
    /// the message was received (if `giaddr` is 0) or on the address of
    /// the relay agent that forwarded the message (`giaddr` when not 0).
    fn get_next_available_addr(&self) -> Result<Ipv4Addr, AddressPoolError> {
        let mut iter = self.available_addrs.iter();
        match iter.next() {
            Some(addr) => Ok(*addr),
            None => Err(AddressPoolError::Ipv4AddrExhaustion),
        }
    }

    fn allocate_addr(&mut self, addr: Ipv4Addr) -> Result<(), AddressPoolError> {
        if self.available_addrs.remove(&addr) {
            self.allocated_addrs.insert(addr);
            Ok(())
        } else {
            Err(AddressPoolError::UnavailableIpv4AddrAllocation(addr))
        }
    }

    fn release_addr(&mut self, addr: Ipv4Addr) -> Result<(), AddressPoolError> {
        if self.allocated_addrs.remove(&addr) {
            self.available_addrs.insert(addr);
            Ok(())
        } else {
            Err(AddressPoolError::UnallocatedIpv4AddrRelease(addr))
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
        value: vec![MessageType::DHCPACK.into()],
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

fn build_nak(req: Message, config: &ServerConfig, error: String) -> Message {
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
    nak.options.push(ConfigOption { code: OptionCode::Message, value: error.into_bytes() });

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
    use std::panic::{self, AssertUnwindSafe};

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

    fn extract_message(server_response: ServerAction) -> Message {
        if let ServerAction::SendResponse(message) = server_response {
            message
        } else {
            panic!("expected a message in server response, received {:?}", server_response)
        }
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

    fn new_test_nak<F>(req: &Message, server: &Server<F>, error: String) -> Message
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
        nak.options.push(ConfigOption { code: OptionCode::Message, value: error.into_bytes() });
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

        assert_eq!(server.dispatch(disc), Ok(ServerAction::SendResponse(expected_offer)));
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

        let offer = extract_message(server.dispatch(disc).unwrap());

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

        assert_eq!(extract_message(response).yiaddr, bound_client_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_client_binding_panics_when_addr_previously_not_allocated(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();

        server.cache.insert(
            disc.chaddr,
            CachedConfig {
                client_addr: bound_client_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        // Make `server` explicitly unwind safe for `cacth_unwind`.
        // https://doc.rust-lang.org/std/panic/struct.AssertUnwindSafe.html
        let mut unwind_safe_server = AssertUnwindSafe(server);
        let result = panic::catch_unwind(move || unwind_safe_server.dispatch(disc));
        assert!(result.is_err());
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

        assert_eq!(extract_message(response).yiaddr, bound_client_ip);
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

        assert_eq!(extract_message(response).yiaddr, free_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_returns_available_requested_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();
        let requested_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        server.pool.available_addrs.insert(requested_ip);

        disc.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: requested_ip.octets().to_vec(),
        });

        server.cache.insert(
            disc.chaddr,
            CachedConfig { client_addr: bound_client_ip, options: vec![], expiration: 0 },
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, requested_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_returns_next_addr_for_unavailable_requested_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();
        let requested_ip = random_ipv4_generator();
        let free_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        server.pool.allocated_addrs.insert(requested_ip);
        server.pool.available_addrs.insert(free_ip);

        disc.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: requested_ip.octets().to_vec(),
        });

        server.cache.insert(
            disc.chaddr,
            CachedConfig { client_addr: bound_client_ip, options: vec![], expiration: 0 },
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip);
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

        assert_eq!(extract_message(response).yiaddr, requested_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_unavailable_requested_addr_returns_next_free_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut disc = new_test_discover();

        let requested_ip = random_ipv4_generator();
        let free_ip_1 = random_ipv4_generator();

        server.pool.allocated_addrs.insert(requested_ip);
        server.pool.available_addrs.insert(free_ip_1);

        disc.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: requested_ip.octets().to_vec(),
        });

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip_1);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_returns_next_addr_for_bad_requested_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();
        let free_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(free_ip);

        // Update `discover` to have bad requested ip.
        disc.options
            .push(ConfigOption { code: OptionCode::RequestedIpAddr, value: vec![100, 200, 1] });

        server.cache.insert(
            disc.chaddr,
            CachedConfig { client_addr: bound_client_ip, options: vec![], expiration: 0 },
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_unavailable_requested_addr_no_available_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut disc = new_test_discover();

        let requested_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(requested_ip);

        disc.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: requested_ip.octets().to_vec(),
        });

        assert_eq!(
            server.dispatch(disc),
            Err(ServerError::ServerAddressPoolFailure(AddressPoolError::Ipv4AddrExhaustion))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_no_requested_addr_no_available_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let disc = new_test_discover();

        assert_eq!(
            server.dispatch(disc),
            Err(ServerError::ServerAddressPoolFailure(AddressPoolError::Ipv4AddrExhaustion))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_offer_message_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;

        // Construct a simple offer sent by client.
        let mut client_offer = Message::new();
        client_offer.op = OpCode::BOOTREQUEST;
        client_offer.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPOFFER.into()],
        });

        assert_eq!(
            server.dispatch(client_offer),
            Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPOFFER))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_ack_message_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;

        // Construct a simple ack sent by client.
        let mut client_ack = Message::new();
        client_ack.op = OpCode::BOOTREQUEST;
        client_ack.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPACK.into()],
        });

        assert_eq!(
            server.dispatch(client_ack),
            Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPACK))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_nak_message_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;

        // Construct a simple nak sent by client.
        let mut client_nak = Message::new();
        client_nak.op = OpCode::BOOTREQUEST;
        client_nak.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPNAK.into()],
        });

        assert_eq!(
            server.dispatch(client_nak),
            Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPNAK))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_returns_correct_ack() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(requested_ip);

        // Update message to request for ip previously offered by server.
        req.ciaddr = requested_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig { client_addr: requested_ip, options: vec![], expiration: std::i64::MAX },
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.ciaddr = requested_ip;
        expected_ack.yiaddr = requested_ip;

        assert_eq!(server.dispatch(req), Ok(ServerAction::SendResponse(expected_ack)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_maintains_server_invariants() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();
        let client_mac = req.chaddr;

        server.pool.allocated_addrs.insert(requested_ip);
        req.ciaddr = requested_ip;
        server.cache.insert(
            client_mac,
            CachedConfig { client_addr: requested_ip, options: vec![], expiration: std::i64::MAX },
        );
        let _response = server.dispatch(req).unwrap();
        assert!(server.cache.contains_key(&client_mac));
        assert!(server.pool.addr_is_allocated(requested_ip));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_wrong_server_ip_returns_error(
    ) -> Result<(), Error> {
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

        assert_eq!(
            server.dispatch(req),
            Err(ServerError::IncorrectDHCPServer(server.config.server_ip))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_unknown_client_mac_returns_error_maintains_server_invariants(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();
        let client_mac = req.chaddr;

        req.ciaddr = requested_ip;

        assert_eq!(server.dispatch(req), Err(ServerError::UnknownClientMac(client_mac)));
        assert!(!server.cache.contains_key(&client_mac));
        assert!(!server.pool.addr_is_allocated(requested_ip));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_mismatched_requested_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let client_requested_ip = random_ipv4_generator();
        let server_offered_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(server_offered_ip);
        req.ciaddr = client_requested_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: server_offered_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        assert_eq!(
            server.dispatch(req),
            Err(ServerError::RequestedIpOfferIpMismatch(client_requested_ip, server_offered_ip,),)
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_expired_client_binding_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(requested_ip);

        req.ciaddr = requested_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig { client_addr: requested_ip, options: vec![], expiration: 0 },
        );

        assert_eq!(server.dispatch(req), Err(ServerError::ExpiredClientConfig));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_no_reserved_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();

        req.ciaddr = requested_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig { client_addr: requested_ip, options: vec![], expiration: std::i64::MAX },
        );

        assert_eq!(server.dispatch(req), Err(ServerError::UnidentifiedRequestedIp(requested_ip)));
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

        assert_eq!(server.dispatch(req), Ok(ServerAction::SendResponse(expected_ack)));
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
        let expected_nak =
            new_test_nak(&req, &server, "client and server are in different subnets".to_owned());
        assert_eq!(server.dispatch(req), Ok(ServerAction::SendResponse(expected_nak)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_unknown_client_mac_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        let client_mac = req.chaddr;

        // Update requested ip and server ip to be on the same subnet.
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: Ipv4Addr::new(192, 165, 30, 45).octets().to_vec(),
        });
        server.config.server_ip = Ipv4Addr::new(192, 165, 30, 1);

        assert_eq!(server.dispatch(req), Err(ServerError::UnknownClientMac(client_mac)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_mismatched_requested_addr_returns_nak(
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

        let server_cached_ip = Ipv4Addr::new(192, 165, 25, 10);
        server.pool.allocated_addrs.insert(server_cached_ip);
        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: server_cached_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        let expected_nak =
            new_test_nak(&req, &server, "requested ip is not assigned to client".to_owned());
        assert_eq!(server.dispatch(req), Ok(ServerAction::SendResponse(expected_nak)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_expired_client_binding_returns_nak(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

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

        let expected_nak =
            new_test_nak(&req, &server, "requested ip is not assigned to client".to_owned());

        assert_eq!(server.dispatch(req), Ok(ServerAction::SendResponse(expected_nak)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_no_reserved_addr_returns_nak() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        let init_reboot_client_ip = Ipv4Addr::new(192, 165, 25, 4);
        req.options.push(ConfigOption {
            code: OptionCode::RequestedIpAddr,
            value: init_reboot_client_ip.octets().to_vec(),
        });
        server.config.server_ip = Ipv4Addr::new(192, 165, 25, 1);

        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: init_reboot_client_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        let expected_nak =
            new_test_nak(&req, &server, "requested ip is not assigned to client".to_owned());

        assert_eq!(server.dispatch(req), Ok(ServerAction::SendResponse(expected_nak)));
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

        assert_eq!(server.dispatch(req), Ok(ServerAction::SendResponse(expected_ack)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_unknown_client_mac_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();
        let client_mac = req.chaddr;

        req.ciaddr = bound_client_ip;

        assert_eq!(server.dispatch(req), Err(ServerError::UnknownClientMac(client_mac)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_mismatched_requested_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        let client_renewal_ip = random_ipv4_generator();
        let bound_client_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        req.ciaddr = client_renewal_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: bound_client_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        assert_eq!(
            server.dispatch(req),
            Err(ServerError::RequestedIpOfferIpMismatch(client_renewal_ip, bound_client_ip))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_expired_client_binding_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        req.ciaddr = bound_client_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig { client_addr: bound_client_ip, options: vec![], expiration: 0 },
        );

        assert_eq!(server.dispatch(req), Err(ServerError::ExpiredClientConfig));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_no_reserved_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();
        req.ciaddr = bound_client_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig {
                client_addr: bound_client_ip,
                options: vec![],
                expiration: std::i64::MAX,
            },
        );

        assert_eq!(
            server.dispatch(req),
            Err(ServerError::UnidentifiedRequestedIp(bound_client_ip))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_unknown_client_state_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;

        let req = new_test_request();

        assert_eq!(server.dispatch(req), Err(ServerError::UnknownClientStateDuringRequest));
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
    async fn test_dispatch_with_client_msg_missing_message_type_option_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut msg = new_test_request();
        msg.options.clear();

        assert_eq!(server.dispatch(msg), Err(ServerError::MissingMessageTypeOption));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_msg_missing_message_type_value_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut msg = new_test_request();
        msg.options.clear();

        msg.options.push(ConfigOption { code: OptionCode::DhcpMessageType, value: vec![] });

        assert_eq!(server.dispatch(msg), Err(ServerError::MissingMessageTypeValue));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_msg_with_unknown_type_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut msg = new_test_request();
        msg.options.clear();

        let invalid_message_type = 123;
        msg.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![invalid_message_type],
        });

        assert_eq!(
            server.dispatch(msg),
            Err(ServerError::UnidentifiedClientMessageType(invalid_message_type))
        );
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

        let client_1_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_1_ip);
        let () = server.store_client_config(
            client_1_ip,
            random_mac_generator(),
            vec![],
            &ClientConfig { lease_time_s: 0 },
        )?;

        let client_2_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_2_ip);
        let () = server.store_client_config(
            client_2_ip,
            random_mac_generator(),
            vec![],
            &ClientConfig { lease_time_s: 0 },
        )?;

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

        let client_1_mac = random_mac_generator();
        let client_1_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_1_ip);
        let () = server.store_client_config(
            client_1_ip,
            client_1_mac,
            vec![],
            &ClientConfig { lease_time_s: std::u32::MAX },
        )?;

        let client_2_mac = random_mac_generator();
        let client_2_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_2_ip);
        let () = server.store_client_config(
            client_2_ip,
            client_2_mac,
            vec![],
            &ClientConfig { lease_time_s: 0 },
        )?;

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

        assert_eq!(server.dispatch(release), Ok(ServerAction::AddressRelease(release_ip)));

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
    async fn test_dispatch_with_unknown_release_maintains_server_state_returns_unknown_mac_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let mut release = new_test_release();

        let release_ip = random_ipv4_generator();
        let client_mac = release.chaddr;

        server.pool.allocated_addrs.insert(release_ip);
        release.ciaddr = release_ip;

        assert_eq!(server.dispatch(release), Err(ServerError::UnknownClientMac(client_mac,)));

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

        assert_eq!(server.dispatch(inform), Ok(ServerAction::SendResponse(expected_ack)));
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

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));

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

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));

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

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));

        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.cache.contains_key(&client_mac), "client config incorrectly retained");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_decline_known_client_for_address_not_in_server_pool_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server(server_time_provider).await?;
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

        assert_eq!(
            server.dispatch(decline),
            Err(ServerError::ServerAddressPoolFailure(
                AddressPoolError::UnavailableIpv4AddrAllocation(declined_ip)
            ))
        );
        Ok(())
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

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));

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

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));

        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.cache.contains_key(&client_mac), "client config incorrectly retained");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_decline_without_requested_addr_returns_error() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server(server_time_provider).await?;
        let decline = new_test_decline(&server);

        assert_eq!(server.dispatch(decline), Err(ServerError::NoRequestedAddrForDecline));
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
                &extract_message(response)
                    .get_config_option(OptionCode::IpAddrLeaseTime)
                    .unwrap()
                    .value
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
                &extract_message(response)
                    .get_config_option(OptionCode::IpAddrLeaseTime)
                    .unwrap()
                    .value
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
