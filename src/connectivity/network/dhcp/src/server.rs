// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::configuration::ServerParameters;
use crate::protocol::{
    DhcpOption, FidlCompatible, Message, MessageType, OpCode, OptionCode, ProtocolError,
};
use crate::stash::Stash;
use failure::{Error, Fail, ResultExt};
use fidl_fuchsia_hardware_ethernet_ext::MacAddress as MacAddr;
use fuchsia_zircon::Status;
use serde_derive::{Deserialize, Serialize};
use std::collections::{BTreeSet, HashMap, HashSet};
use std::convert::TryFrom;
use std::fmt;
use std::net::Ipv4Addr;

/// A minimal DHCP server.
///
/// This comment will be expanded upon in future CLs as the server design
/// is iterated upon.
pub struct Server {
    cache: CachedClients,
    pool: AddressPool,
    params: ServerParameters,
    stash: Stash,
    options_repo: HashMap<OptionCode, DhcpOption>,
}

/// The default string used by the Server to identify itself to the Stash service.
pub const DEFAULT_STASH_ID: &str = "dhcpd";
/// The default prefix used by the Server in the keys for values stored in the Stash service.
pub const DEFAULT_STASH_PREFIX: &str = "";

/// This enumerates the actions a DHCP server can take in response to a
/// received client message. A `SendResponse(Message, Ipv4Addr)` indicates
/// that a `Message` needs to be delivered back to the client.
/// The server may optionally send a destination `Ipv4Addr` (if the protocol
/// warrants it) to direct the response `Message` to.
/// The other two variants indicate a successful processing of a client
/// `Decline` or `Release`.
/// Implements `PartialEq` for test assertions.
#[derive(Debug, PartialEq)]
pub enum ServerAction {
    SendResponse(Message, Option<Ipv4Addr>),
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

    #[fail(display = "client request error: {}", _0)]
    ClientMessageError(ProtocolError),

    #[fail(display = "error manipulating server cache: {}", _0)]
    ServerCacheUpdateFailure(StashError),

    #[fail(display = "server not configured with an ip address")]
    ServerMissingIpAddr,

    #[fail(display = "missing required dhcp option: {:?}", _0)]
    MissingRequiredDhcpOption(OptionCode),

    #[fail(display = "unable to get system time")]
    // The underlying error is not provided to this variant as it (std::time::SystemTimeError) does
    // not implement PartialEq.
    ServerTimeError,
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

impl Server {
    /// Instantiates a server with a random stash identifier.
    /// Used in tests to ensure that each test has an isolated stash instance.
    #[cfg(test)]
    pub async fn new_test_server() -> Result<Server, Error> {
        use rand::Rng;
        let rand_string: String =
            rand::thread_rng().sample_iter(&rand::distributions::Alphanumeric).take(8).collect();
        let lease_length = crate::configuration::LeaseLength {
            default_seconds: 60 * 60 * 24,
            max_seconds: 60 * 60 * 24 * 7,
        };
        let params = ServerParameters {
            server_ips: vec![],
            lease_length,
            managed_addrs: crate::configuration::ManagedAddresses {
                network_id: Ipv4Addr::new(192, 168, 0, 0),
                broadcast: Ipv4Addr::new(192, 168, 0, 255),
                mask: crate::configuration::SubnetMask::try_from(24)?,
                pool_range_start: Ipv4Addr::new(192, 168, 0, 0),
                pool_range_stop: Ipv4Addr::new(192, 168, 0, 0),
            },
            permitted_macs: Vec::new(),
            static_assignments: HashMap::new(),
        };
        Server::from_config(params, &rand_string, DEFAULT_STASH_PREFIX).await
    }

    /// Instantiates a `Server` value from the provided `ServerConfig`.
    pub async fn from_config<'a>(
        params: ServerParameters,
        stash_id: &'a str,
        stash_prefix: &'a str,
    ) -> Result<Server, Error> {
        let stash = Stash::new(stash_id, stash_prefix).context("failed to instantiate stash")?;
        let cache = match stash.load().await {
            Ok(c) => c,
            Err(e) => {
                log::info!("attempted to load stored leases from an empty stash: {}", e);
                HashMap::new()
            }
        };
        let server = Server {
            cache,
            pool: AddressPool::new(params.managed_addrs.pool_range()),
            params,
            stash,
            options_repo: HashMap::new(),
        };
        Ok(server)
    }

    /// Returns true if the server has a populated address pool and is therefore serving requests.
    pub fn is_serving(&self) -> bool {
        !self.pool.is_empty()
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
        match msg.get_dhcp_type().map_err(ServerError::ClientMessageError)? {
            MessageType::DHCPDISCOVER => self.handle_discover(msg),
            MessageType::DHCPOFFER => {
                Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPOFFER))
            }
            MessageType::DHCPREQUEST => self.handle_request(msg),
            MessageType::DHCPDECLINE => self.handle_decline(msg),
            MessageType::DHCPACK => {
                Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPACK))
            }
            MessageType::DHCPNAK => {
                Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPNAK))
            }
            MessageType::DHCPRELEASE => self.handle_release(msg),
            MessageType::DHCPINFORM => self.handle_inform(msg),
        }
    }

    /// This method calculates the destination address of the server response
    /// based on the conditions specified in -
    /// https://tools.ietf.org/html/rfc2131#section-4.1 Page 22, Paragraph 4.
    fn get_destination_addr(&mut self, client_msg: &Message) -> Option<Ipv4Addr> {
        if !client_msg.giaddr.is_unspecified() {
            Some(client_msg.giaddr)
        } else if !client_msg.ciaddr.is_unspecified() {
            Some(client_msg.ciaddr)
        } else if client_msg.bdcast_flag {
            Some(Ipv4Addr::BROADCAST)
        } else {
            client_msg.get_dhcp_type().ok().and_then(|typ| {
                match typ {
                    // TODO(fxbug.dev/35087): Revisit the first match arm.
                    // Instead of returning BROADCAST address, server must update ARP table.
                    //
                    // Current Implementation =>
                    // When client's message has unspecified `giaddr`, 'ciaddr' with
                    // broadcast bit is not set, we broadcast the response on the subnet.
                    //
                    // Desired Implementation =>
                    // Message should be unicast to client's mac address specified in `chaddr`.
                    //
                    // See https://tools.ietf.org/html/rfc2131#section-4.1 Page 22, Paragraph 4.
                    MessageType::DHCPDISCOVER => Some(Ipv4Addr::BROADCAST),
                    MessageType::DHCPREQUEST | MessageType::DHCPINFORM => Some(client_msg.yiaddr),
                    MessageType::DHCPACK
                    | MessageType::DHCPNAK
                    | MessageType::DHCPOFFER
                    | MessageType::DHCPDECLINE
                    | MessageType::DHCPRELEASE => None,
                }
            })
        }
    }

    fn handle_discover(&mut self, disc: Message) -> Result<ServerAction, ServerError> {
        let offered_ip = self.get_addr(&disc)?;
        let dest = self.get_destination_addr(&disc);
        let offer = self.build_offer(disc, offered_ip)?;
        match self.store_client_config(Ipv4Addr::from(offer.yiaddr), offer.chaddr, &offer.options) {
            Ok(()) => Ok(ServerAction::SendResponse(offer, dest)),
            Err(e) => Err(ServerError::ServerCacheUpdateFailure(StashError { error: e })),
        }
    }

    fn get_addr(&mut self, client: &Message) -> Result<Ipv4Addr, ServerError> {
        if let Some(config) = self.cache.get(&client.chaddr) {
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_err(|std::time::SystemTimeError { .. }| ServerError::ServerTimeError)?;
            if !config.expired(now) {
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
        if let Some(requested_addr) = get_requested_ip_addr(&client) {
            if self.pool.addr_is_available(requested_addr) {
                return Ok(requested_addr);
            }
        }
        self.pool.get_next_available_addr().map_err(AddressPoolError::into)
    }

    fn store_client_config(
        &mut self,
        client_addr: Ipv4Addr,
        client_mac: MacAddr,
        client_opts: &[DhcpOption],
    ) -> Result<(), Error> {
        let lease_length_seconds = client_opts
            .iter()
            .filter_map(|opt| match opt {
                DhcpOption::IpAddressLeaseTime(v) => Some(*v),
                _ => None,
            })
            .next()
            .ok_or(ServerError::MissingRequiredDhcpOption(OptionCode::IpAddressLeaseTime))?;
        let options = client_opts
            .iter()
            .filter(|opt| {
                // DhcpMessageType is part of transaction semantics and should not be stored.
                opt.code() != OptionCode::DhcpMessageType
            })
            .cloned()
            .collect();
        let config = CachedConfig::new(
            client_addr,
            options,
            std::time::SystemTime::now(),
            lease_length_seconds,
        )?;
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
        match get_client_state(&req).map_err(|()| ServerError::UnknownClientStateDuringRequest)? {
            ClientState::Selecting => self.handle_request_selecting(req),
            ClientState::InitReboot => self.handle_request_init_reboot(req),
            ClientState::Renewing => self.handle_request_renewing(req),
        }
    }

    fn handle_request_selecting(&mut self, req: Message) -> Result<ServerAction, ServerError> {
        let requested_ip = req.ciaddr;
        if !is_recipient(&self.params.server_ips, &req) {
            Err(ServerError::IncorrectDHCPServer(
                *self.params.server_ips.first().ok_or(ServerError::ServerMissingIpAddr)?,
            ))
        } else {
            let () = self.validate_requested_addr_with_client(&req, requested_ip)?;
            let dest = self.get_destination_addr(&req);
            Ok(ServerAction::SendResponse(self.build_ack(req, requested_ip)?, dest))
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
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_err(|std::time::SystemTimeError { .. }| ServerError::ServerTimeError)?;
            if client_config.client_addr != requested_ip {
                Err(ServerError::RequestedIpOfferIpMismatch(
                    requested_ip,
                    client_config.client_addr,
                ))
            } else if client_config.expired(now) {
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
        if !is_in_subnet(&req, &self.params) {
            let error_msg = "client and server are in different subnets";
            let (nak, dest) = self.build_nak(req, error_msg)?;
            return Ok(ServerAction::SendResponse(nak, dest));
        }
        if !is_client_mac_known(req.chaddr, &self.cache) {
            return Err(ServerError::UnknownClientMac(req.chaddr));
        }
        if self.validate_requested_addr_with_client(&req, requested_ip).is_err() {
            let error_msg = "requested ip is not assigned to client";
            let (nak, dest) = self.build_nak(req, error_msg)?;
            return Ok(ServerAction::SendResponse(nak, dest));
        }
        let dest = self.get_destination_addr(&req);
        Ok(ServerAction::SendResponse(self.build_ack(req, requested_ip)?, dest))
    }

    fn handle_request_renewing(&mut self, req: Message) -> Result<ServerAction, ServerError> {
        let client_ip = req.ciaddr;
        let () = self.validate_requested_addr_with_client(&req, client_ip)?;
        let dest = self.get_destination_addr(&req);
        Ok(ServerAction::SendResponse(self.build_ack(req, client_ip)?, dest))
    }

    /// TODO(fxbug.dev/21422): Ensure server behavior is as intended.
    fn handle_decline(&mut self, dec: Message) -> Result<ServerAction, ServerError> {
        let declined_ip =
            get_requested_ip_addr(&dec).ok_or_else(|| ServerError::NoRequestedAddrForDecline)?;
        if is_recipient(&self.params.server_ips, &dec)
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
        let yiaddr = Ipv4Addr::UNSPECIFIED;
        let dest = self.get_destination_addr(&inf);
        let ack = self.build_inform_ack(inf, yiaddr)?;
        Ok(ServerAction::SendResponse(ack, dest))
    }

    fn build_offer(&self, disc: Message, offered_ip: Ipv4Addr) -> Result<Message, ServerError> {
        let mut options = Vec::new();
        options.push(DhcpOption::DhcpMessageType(MessageType::DHCPOFFER));
        options.push(DhcpOption::ServerIdentifier(self.get_server_ip(&disc)?));
        let seconds = match disc
            .options
            .iter()
            .filter_map(|opt| match opt {
                DhcpOption::IpAddressLeaseTime(seconds) => Some(*seconds),
                _ => None,
            })
            .next()
        {
            Some(seconds) => std::cmp::min(seconds, self.params.lease_length.max_seconds),
            None => self.params.lease_length.default_seconds,
        };
        options.push(DhcpOption::IpAddressLeaseTime(seconds));
        options.push(DhcpOption::RenewalTimeValue(seconds / 2));
        options.push(DhcpOption::RebindingTimeValue((seconds * 3) / 4));
        options.extend_from_slice(&self.get_requested_options(&disc.options));
        let offer = Message {
            op: OpCode::BOOTREPLY,
            secs: 0,
            yiaddr: offered_ip,
            ciaddr: Ipv4Addr::UNSPECIFIED,
            siaddr: Ipv4Addr::UNSPECIFIED,
            sname: String::new(),
            file: String::new(),
            options,
            ..disc
        };
        Ok(offer)
    }

    fn get_requested_options(&self, client_opts: &[DhcpOption]) -> Vec<DhcpOption> {
        if let Some(requested_opts) = client_opts
            .iter()
            .filter_map(|opt| match opt {
                DhcpOption::ParameterRequestList(v) => Some(v),
                _ => None,
            })
            .next()
        {
            let offered_opts: Vec<DhcpOption> = requested_opts
                .iter()
                .filter_map(|code| match self.options_repo.get(code) {
                    Some(opt) => Some(opt.clone()),
                    None => match code {
                        OptionCode::SubnetMask => {
                            Some(DhcpOption::SubnetMask(self.params.managed_addrs.mask.into()))
                        }
                        _ => None,
                    },
                })
                .collect();
            offered_opts
        } else {
            Vec::new()
        }
    }

    fn build_ack(&self, req: Message, requested_ip: Ipv4Addr) -> Result<Message, ServerError> {
        let options = match self.cache.get(&req.chaddr) {
            Some(config) => {
                let mut options = config.options.clone();
                options.push(DhcpOption::DhcpMessageType(MessageType::DHCPACK));
                options
            }
            None => return Err(ServerError::UnknownClientMac(req.chaddr)),
        };
        let ack = Message { op: OpCode::BOOTREPLY, secs: 0, yiaddr: requested_ip, options, ..req };
        Ok(ack)
    }

    fn build_inform_ack(&self, inf: Message, client_ip: Ipv4Addr) -> Result<Message, ServerError> {
        let server_ip = self.get_server_ip(&inf)?;
        let mut options = Vec::new();
        options.push(DhcpOption::DhcpMessageType(MessageType::DHCPACK));
        options.push(DhcpOption::ServerIdentifier(server_ip));
        options.extend_from_slice(&self.get_requested_options(&inf.options));
        let ack = Message { op: OpCode::BOOTREPLY, secs: 0, yiaddr: client_ip, options, ..inf };
        Ok(ack)
    }

    fn build_nak(
        &self,
        req: Message,
        error: &str,
    ) -> Result<(Message, Option<Ipv4Addr>), ServerError> {
        let options = vec![
            DhcpOption::DhcpMessageType(MessageType::DHCPNAK),
            DhcpOption::ServerIdentifier(self.get_server_ip(&req)?),
            DhcpOption::Message(error.to_owned()),
        ];
        let mut nak = Message {
            op: OpCode::BOOTREPLY,
            secs: 0,
            ciaddr: Ipv4Addr::UNSPECIFIED,
            yiaddr: Ipv4Addr::UNSPECIFIED,
            siaddr: Ipv4Addr::UNSPECIFIED,
            options,
            ..req
        };
        // https://tools.ietf.org/html/rfc2131#section-4.3.2
        // Page 31, Paragraph 2-3.
        if nak.giaddr.is_unspecified() {
            Ok((nak, Some(Ipv4Addr::BROADCAST)))
        } else {
            nak.bdcast_flag = true;
            Ok((nak, None))
        }
    }

    fn get_server_ip(&self, req: &Message) -> Result<Ipv4Addr, ServerError> {
        match req
            .options
            .iter()
            .filter_map(|opt| match opt {
                DhcpOption::ServerIdentifier(v) => Some(v),
                _ => None,
            })
            .next()
        {
            Some(v) if self.params.server_ips.contains(v) => Ok(*v),
            _ => Ok(*self.params.server_ips.first().ok_or(ServerError::ServerMissingIpAddr)?),
        }
    }

    /// Releases all allocated IP addresses whose leases have expired back to
    /// the pool of addresses available for allocation.
    pub fn release_expired_leases(&mut self) -> Result<(), ServerError> {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_err(|std::time::SystemTimeError { .. }| ServerError::ServerTimeError)?;
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
        Ok(())
    }
}

/// Clears the stash instance at the end of a test.
///
/// This implementation is solely for unit testing, where we do not want data stored in
/// the stash to persist past the execution of the test.
#[cfg(test)]
impl Drop for Server {
    fn drop(&mut self) {
        if !cfg!(test) {
            panic!("dhcp::server::Server implements std::ops::Drop in a non-test cfg");
        }
        let _result = self.stash.clear();
    }
}

/// The ability to dispatch fuchsia.net.dhcp.Server protocol requests and return a value.
///
/// Implementers of this trait can be used as the backing server-side logic of the
/// fuchsia.net.dhcp.Server protocol. Implementers must maintain a store of DHCP Options and
/// DHCP server parameters and support the trait methods to retrieve and modify them.
pub trait ServerDispatcher {
    /// Retrieves the stored DHCP option value that corresponds to the OptionCode argument.
    fn dispatch_get_option(
        &self,
        code: fidl_fuchsia_net_dhcp::OptionCode,
    ) -> Result<fidl_fuchsia_net_dhcp::Option_, Status>;
    /// Retrieves the stored DHCP server parameter value that corresponds to the ParameterName argument.
    fn dispatch_get_parameter(
        &self,
        name: fidl_fuchsia_net_dhcp::ParameterName,
    ) -> Result<fidl_fuchsia_net_dhcp::Parameter, Status>;
    /// Updates the stored DHCP option value to the argument.
    fn dispatch_set_option(&mut self, value: fidl_fuchsia_net_dhcp::Option_) -> Result<(), Status>;
    /// Updates the stored DHCP server parameter to the argument.
    fn dispatch_set_parameter(
        &mut self,
        value: fidl_fuchsia_net_dhcp::Parameter,
    ) -> Result<(), Status>;
    /// Retrieves all of the stored DHCP option values.
    fn dispatch_list_options(&self) -> Result<Vec<fidl_fuchsia_net_dhcp::Option_>, Status>;
    /// Retrieves all of the stored DHCP parameter values.
    fn dispatch_list_parameters(&self) -> Result<Vec<fidl_fuchsia_net_dhcp::Parameter>, Status>;
}

impl ServerDispatcher for Server {
    fn dispatch_get_option(
        &self,
        code: fidl_fuchsia_net_dhcp::OptionCode,
    ) -> Result<fidl_fuchsia_net_dhcp::Option_, Status> {
        let opt_code =
            OptionCode::try_from(code as u8).map_err(|_protocol_error| Status::INVALID_ARGS)?;
        let option = self.options_repo.get(&opt_code).ok_or(Status::NOT_FOUND)?;
        let option = option.clone();
        let fidl_option = option.try_into_fidl().map_err(|protocol_error| {
            log::warn!(
                "server dispatcher could not convert dhcp option for fidl transport: {}",
                protocol_error
            );
            Status::INTERNAL
        })?;
        Ok(fidl_option)
    }

    fn dispatch_get_parameter(
        &self,
        _name: fidl_fuchsia_net_dhcp::ParameterName,
    ) -> Result<fidl_fuchsia_net_dhcp::Parameter, Status> {
        unimplemented!()
    }

    fn dispatch_set_option(&mut self, value: fidl_fuchsia_net_dhcp::Option_) -> Result<(), Status> {
        let option = DhcpOption::try_from_fidl(value).map_err(|protocol_error| {
            log::warn!(
                "server dispatcher could not convert fidl argument into dhcp option: {}",
                protocol_error
            );
            Status::INVALID_ARGS
        })?;
        let _old = self.options_repo.insert(option.code(), option);
        Ok(())
    }

    fn dispatch_set_parameter(
        &mut self,
        _value: fidl_fuchsia_net_dhcp::Parameter,
    ) -> Result<(), Status> {
        unimplemented!()
    }

    fn dispatch_list_options(&self) -> Result<Vec<fidl_fuchsia_net_dhcp::Option_>, Status> {
        let options = self
            .options_repo
            .values()
            .filter_map(|option| {
                option
                    .clone()
                    .try_into_fidl()
                    .map_err(|protocol_error| {
                        log::warn!(
                        "server dispatcher could not convert dhcp option for fidl transport: {}",
                        protocol_error
                    );
                        Status::INTERNAL
                    })
                    .ok()
            })
            .collect::<Vec<fidl_fuchsia_net_dhcp::Option_>>();
        Ok(options)
    }

    fn dispatch_list_parameters(&self) -> Result<Vec<fidl_fuchsia_net_dhcp::Parameter>, Status> {
        unimplemented!()
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
#[derive(Debug, Deserialize, Serialize)]
pub struct CachedConfig {
    client_addr: Ipv4Addr,
    options: Vec<DhcpOption>,
    lease_start_epoch_seconds: u64,
    lease_length_seconds: u32,
}

#[cfg(test)]
impl Default for CachedConfig {
    fn default() -> Self {
        CachedConfig {
            client_addr: Ipv4Addr::UNSPECIFIED,
            options: vec![],
            lease_start_epoch_seconds: std::u64::MIN,
            lease_length_seconds: std::u32::MAX,
        }
    }
}

impl PartialEq for CachedConfig {
    fn eq(&self, other: &Self) -> bool {
        // Only compare directly comparable fields.
        let CachedConfig {
            client_addr,
            options,
            lease_start_epoch_seconds: _not_comparable,
            lease_length_seconds,
        } = self;
        let CachedConfig {
            client_addr: other_client_addr,
            options: other_options,
            lease_start_epoch_seconds: _other_not_comparable,
            lease_length_seconds: other_lease_length_seconds,
        } = other;
        client_addr == other_client_addr
            && options == other_options
            && lease_length_seconds == other_lease_length_seconds
    }
}

impl CachedConfig {
    fn new(
        client_addr: Ipv4Addr,
        options: Vec<DhcpOption>,
        lease_start: std::time::SystemTime,
        lease_length_seconds: u32,
    ) -> Result<Self, Error> {
        let lease_start_epoch_seconds =
            lease_start.duration_since(std::time::UNIX_EPOCH)?.as_secs();
        Ok(Self { client_addr, options, lease_start_epoch_seconds, lease_length_seconds })
    }

    fn expired(&self, since_unix_epoch: std::time::Duration) -> bool {
        let end = std::time::Duration::from_secs(
            self.lease_start_epoch_seconds + self.lease_length_seconds as u64,
        );
        since_unix_epoch >= end
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
    fn new<T: Iterator<Item = Ipv4Addr>>(addrs: T) -> Self {
        Self { available_addrs: addrs.collect(), allocated_addrs: HashSet::new() }
    }

    /// TODO(fxbug.dev/21423): The ip should be handed out based on client subnet
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

    fn is_empty(&self) -> bool {
        self.available_addrs.len() == 0 && self.allocated_addrs.len() == 0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ClientState {
    Selecting,
    InitReboot,
    Renewing,
}

fn is_recipient(server_ips: &Vec<Ipv4Addr>, req: &Message) -> bool {
    if let Some(server_id) = get_server_id_from(&req) {
        server_ips.contains(&server_id)
    } else {
        false
    }
}

fn is_in_subnet(req: &Message, config: &ServerParameters) -> bool {
    let client_ip = match get_requested_ip_addr(&req) {
        Some(ip) => ip,
        None => return false,
    };
    config.server_ips.iter().any(|server_ip| {
        config.managed_addrs.mask.apply_to(client_ip)
            == config.managed_addrs.mask.apply_to(*server_ip)
    })
}

fn is_client_mac_known(mac: MacAddr, cache: &CachedClients) -> bool {
    cache.get(&mac).is_some()
}

fn get_client_state(msg: &Message) -> Result<ClientState, ()> {
    let have_server_id = get_server_id_from(&msg).is_some();
    let have_requested_ip = get_requested_ip_addr(&msg).is_some();

    if msg.ciaddr.is_unspecified() {
        if have_requested_ip {
            Ok(ClientState::InitReboot)
        } else {
            Err(())
        }
    } else {
        if have_server_id && !have_requested_ip {
            Ok(ClientState::Selecting)
        } else {
            Ok(ClientState::Renewing)
        }
    }
}

fn get_requested_ip_addr(req: &Message) -> Option<Ipv4Addr> {
    req.options
        .iter()
        .filter_map(
            |opt| {
                if let DhcpOption::RequestedIpAddress(addr) = opt {
                    Some(*addr)
                } else {
                    None
                }
            },
        )
        .next()
}

fn get_server_id_from(req: &Message) -> Option<Ipv4Addr> {
    req.options
        .iter()
        .filter_map(
            |opt| {
                if let DhcpOption::ServerIdentifier(addr) = opt {
                    Some(*addr)
                } else {
                    None
                }
            },
        )
        .next()
}

#[cfg(test)]
pub mod tests {

    use super::*;
    use crate::configuration::LeaseLength;
    use crate::protocol::{DhcpOption, Message, MessageType, OpCode, OptionCode};
    use rand::Rng;
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

    fn extract_message(server_response: ServerAction) -> Message {
        if let ServerAction::SendResponse(message, _destination) = server_response {
            message
        } else {
            panic!("expected a message in server response, received {:?}", server_response)
        }
    }

    async fn new_test_minimal_server() -> Result<Server, Error> {
        let mut server = Server::new_test_server().await.context("failed to instantiate server")?;

        server.params.server_ips = vec![random_ipv4_generator()];
        let ll = LeaseLength { default_seconds: 100, max_seconds: 60 * 60 * 24 * 7 };
        server.params.lease_length = ll;
        server
            .options_repo
            .insert(OptionCode::Router, DhcpOption::Router(vec![random_ipv4_generator()]));
        server.options_repo.insert(
            OptionCode::DomainNameServer,
            DhcpOption::DomainNameServer(vec![
                Ipv4Addr::new(8, 8, 8, 8),
                Ipv4Addr::new(8, 8, 4, 4),
            ]),
        );
        Ok(server)
    }

    fn new_test_discover() -> Message {
        let mut disc = Message::new();
        disc.xid = rand::thread_rng().gen();
        disc.chaddr = random_mac_generator();
        disc.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPDISCOVER));
        disc.options.push(DhcpOption::ParameterRequestList(vec![
            OptionCode::SubnetMask,
            OptionCode::Router,
            OptionCode::DomainNameServer,
        ]));
        disc
    }

    // Creating a new offer needs a reference to `discover` and `server`
    // so it can copy over the essential randomly generated options.
    fn new_test_offer(disc: &Message, server: &Server) -> Message {
        let mut offer = Message::new();
        offer.op = OpCode::BOOTREPLY;
        offer.xid = disc.xid;
        offer.chaddr = disc.chaddr;
        offer.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPOFFER));
        offer.options.push(DhcpOption::ServerIdentifier(
            server.get_server_ip(&disc).unwrap_or(Ipv4Addr::UNSPECIFIED),
        ));
        offer.options.push(DhcpOption::IpAddressLeaseTime(100));
        offer.options.push(DhcpOption::RenewalTimeValue(50));
        offer.options.push(DhcpOption::RebindingTimeValue(75));
        offer.options.push(DhcpOption::SubnetMask(Ipv4Addr::new(255, 255, 255, 0)));
        if let Some(routers) = match server.options_repo.get(&OptionCode::Router) {
            Some(DhcpOption::Router(v)) => Some(v),
            _ => None,
        } {
            offer.options.push(DhcpOption::Router(routers.clone()));
        }
        if let Some(servers) = match server.options_repo.get(&OptionCode::DomainNameServer) {
            Some(DhcpOption::DomainNameServer(v)) => Some(v),
            _ => None,
        } {
            offer.options.push(DhcpOption::DomainNameServer(servers.clone()));
        }
        offer
    }

    fn new_test_request() -> Message {
        let mut req = Message::new();
        req.xid = rand::thread_rng().gen();
        req.chaddr = random_mac_generator();
        req.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPREQUEST));
        req.options.push(DhcpOption::ParameterRequestList(vec![
            OptionCode::SubnetMask,
            OptionCode::Router,
            OptionCode::DomainNameServer,
        ]));
        req
    }

    fn new_test_request_selecting_state(server: &Server) -> Message {
        let mut req = new_test_request();
        req.options.push(DhcpOption::ServerIdentifier(
            server.get_server_ip(&req).unwrap_or(Ipv4Addr::UNSPECIFIED),
        ));
        req
    }

    fn new_test_ack(req: &Message, server: &Server) -> Message {
        let mut ack = Message::new();
        ack.op = OpCode::BOOTREPLY;
        ack.xid = req.xid;
        ack.chaddr = req.chaddr;
        ack.options.push(DhcpOption::ServerIdentifier(
            server.get_server_ip(&req).unwrap_or(Ipv4Addr::UNSPECIFIED),
        ));
        ack.options.push(DhcpOption::IpAddressLeaseTime(100));
        ack.options.push(DhcpOption::RenewalTimeValue(50));
        ack.options.push(DhcpOption::RebindingTimeValue(75));
        ack.options.push(DhcpOption::SubnetMask(Ipv4Addr::new(255, 255, 255, 0)));
        if let Some(routers) = match server.options_repo.get(&OptionCode::Router) {
            Some(DhcpOption::Router(v)) => Some(v),
            _ => None,
        } {
            ack.options.push(DhcpOption::Router(routers.clone()));
        }
        if let Some(servers) = match server.options_repo.get(&OptionCode::DomainNameServer) {
            Some(DhcpOption::DomainNameServer(v)) => Some(v),
            _ => None,
        } {
            ack.options.push(DhcpOption::DomainNameServer(servers.clone()));
        }
        ack.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPACK));
        ack
    }

    fn new_test_nak(req: &Message, server: &Server, error: String) -> Message {
        let mut nak = Message::new();
        nak.op = OpCode::BOOTREPLY;
        nak.xid = req.xid;
        nak.chaddr = req.chaddr;
        nak.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPNAK));
        nak.options.push(DhcpOption::ServerIdentifier(
            server.get_server_ip(&req).unwrap_or(Ipv4Addr::UNSPECIFIED),
        ));
        nak.options.push(DhcpOption::Message(error));
        nak
    }

    fn new_test_release() -> Message {
        let mut release = Message::new();
        release.xid = rand::thread_rng().gen();
        release.chaddr = random_mac_generator();
        release.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPRELEASE));
        release
    }

    fn new_test_inform() -> Message {
        let mut inform = Message::new();
        inform.xid = rand::thread_rng().gen();
        inform.chaddr = random_mac_generator();
        inform.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPINFORM));
        inform
    }

    fn new_test_inform_ack(req: &Message, server: &Server) -> Message {
        let mut ack = Message::new();
        ack.op = OpCode::BOOTREPLY;
        ack.xid = req.xid;
        ack.chaddr = req.chaddr;
        ack.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPACK));
        ack.options.push(DhcpOption::ServerIdentifier(
            server.get_server_ip(&req).unwrap_or(Ipv4Addr::UNSPECIFIED),
        ));
        ack
    }

    fn new_test_decline(server: &Server) -> Message {
        let mut decline = Message::new();
        decline.xid = rand::thread_rng().gen();
        decline.chaddr = random_mac_generator();
        decline.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPDECLINE));
        decline.options.push(DhcpOption::ServerIdentifier(
            server.get_server_ip(&decline).unwrap_or(Ipv4Addr::UNSPECIFIED),
        ));
        decline
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_returns_correct_offer_and_dest_giaddr_when_giaddr_set(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();
        disc.giaddr = random_ipv4_generator();

        let offer_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(offer_ip);

        let mut expected_offer = new_test_offer(&disc, &server);
        expected_offer.yiaddr = offer_ip;
        expected_offer.giaddr = disc.giaddr;

        let expected_dest = disc.giaddr;

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(expected_offer, Some(expected_dest)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_returns_correct_offer_and_dest_ciaddr_when_giaddr_unspecified(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();
        disc.ciaddr = random_ipv4_generator();

        let offer_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(offer_ip);

        let mut expected_offer = new_test_offer(&disc, &server);
        expected_offer.yiaddr = offer_ip;

        let expected_dest = disc.ciaddr;

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(expected_offer, Some(expected_dest)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_broadcast_bit_set_returns_correct_offer_and_dest_broadcast_when_giaddr_and_ciaddr_unspecified(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();
        disc.bdcast_flag = true;

        let offer_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(offer_ip);

        let mut expected_offer = new_test_offer(&disc, &server);
        expected_offer.yiaddr = offer_ip;
        expected_offer.bdcast_flag = true;

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(expected_offer, Some(Ipv4Addr::BROADCAST)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_returns_correct_offer_and_dest_broadcast_when_giaddr_and_ciaddr_unspecified_and_broadcast_bit_unset(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let disc = new_test_discover();

        let offer_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(offer_ip);

        let mut expected_offer = new_test_offer(&disc, &server);
        expected_offer.yiaddr = offer_ip;

        // TODO(fxbug.dev/35087): Instead of returning BROADCAST address, server must update ARP table.
        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(expected_offer, Some(Ipv4Addr::BROADCAST)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_returns_correct_offer_and_dest_giaddr_if_giaddr_ciaddr_broadcast_bit_is_set(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();
        disc.giaddr = random_ipv4_generator();
        disc.ciaddr = random_ipv4_generator();
        disc.bdcast_flag = true;

        let offer_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(offer_ip);

        let mut expected_offer = new_test_offer(&disc, &server);
        expected_offer.yiaddr = offer_ip;
        expected_offer.giaddr = disc.giaddr;
        expected_offer.bdcast_flag = true;

        let expected_dest = disc.giaddr;

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(expected_offer, Some(expected_dest)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_returns_correct_offer_and_dest_ciaddr_if_ciaddr_broadcast_bit_is_set(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();
        disc.ciaddr = random_ipv4_generator();
        disc.bdcast_flag = true;

        let offer_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(offer_ip);

        let mut expected_offer = new_test_offer(&disc, &server);
        expected_offer.yiaddr = offer_ip;
        expected_offer.bdcast_flag = true;

        let expected_dest = disc.ciaddr;

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(expected_offer, Some(expected_dest)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_updates_server_state() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let disc = new_test_discover();

        let offer_ip = random_ipv4_generator();
        let client_mac = disc.chaddr;

        server.pool.available_addrs.insert(offer_ip);

        let server_id = server.params.server_ips.first().unwrap();
        let router = match server.options_repo.get(&OptionCode::Router) {
            Some(DhcpOption::Router(router)) => Some(router.clone()),
            _ => None,
        }
        .ok_or(ProtocolError::MissingOption(OptionCode::Router))?;
        let dns_server = match server.options_repo.get(&OptionCode::DomainNameServer) {
            Some(DhcpOption::DomainNameServer(dns_server)) => Some(dns_server.clone()),
            _ => None,
        }
        .ok_or(ProtocolError::MissingOption(OptionCode::DomainNameServer))?;
        let expected_client_config = CachedConfig::new(
            offer_ip,
            vec![
                DhcpOption::ServerIdentifier(*server_id),
                DhcpOption::IpAddressLeaseTime(server.params.lease_length.default_seconds),
                DhcpOption::RenewalTimeValue(server.params.lease_length.default_seconds / 2),
                DhcpOption::RebindingTimeValue(
                    (server.params.lease_length.default_seconds * 3) / 4,
                ),
                DhcpOption::SubnetMask(Ipv4Addr::new(255, 255, 255, 0)),
                DhcpOption::Router(router),
                DhcpOption::DomainNameServer(dns_server),
            ],
            std::time::SystemTime::now(),
            server.params.lease_length.default_seconds,
        )?;

        let _response = server.dispatch(disc);

        assert_eq!(server.pool.available_addrs.len(), 0);
        assert_eq!(server.pool.allocated_addrs.len(), 1);
        assert_eq!(server.cache.len(), 1);
        assert_eq!(server.cache.get(&client_mac), Some(&expected_client_config));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_updates_stash() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let disc = new_test_discover();

        let offer_ip = random_ipv4_generator();
        let client_mac = disc.chaddr;

        server.pool.available_addrs.insert(offer_ip);

        let offer = extract_message(server.dispatch(disc).unwrap());

        let accessor = server.stash.clone_proxy();
        let value = accessor
            .get_value(&format!("{}-{}", DEFAULT_STASH_PREFIX, client_mac))
            .await
            .context("failed to get value from stash")?;
        let value = value.ok_or(failure::err_msg("value not contained in stash"))?;
        let serialized_config = match value.as_ref() {
            fidl_fuchsia_stash::Value::Stringval(s) => Ok(s),
            val => Err(failure::format_err!("unexpected value in stash: {:?}", val)),
        }?;
        let deserialized_config = serde_json::from_str::<CachedConfig>(serialized_config)
            .context("failed to deserialize config")?;

        assert_eq!(deserialized_config.client_addr, offer.yiaddr);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_client_binding_returns_bound_addr() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);

        server.cache.insert(
            disc.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, bound_client_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic(expected = "tried to release unallocated ip")]
    async fn test_dispatch_with_discover_client_binding_panics_when_addr_previously_not_allocated()
    {
        let mut server = new_test_minimal_server().await.unwrap();
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();

        server.cache.insert(
            disc.chaddr,
            CachedConfig::new(bound_client_ip, vec![], std::time::SystemTime::now(), std::u32::MAX)
                .unwrap(),
        );

        let _ = server.dispatch(disc);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_returns_available_old_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();

        server.pool.available_addrs.insert(bound_client_ip);

        server.cache.insert(
            disc.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MIN,
            )?,
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, bound_client_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_unavailable_addr_returns_next_free_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();
        let free_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        server.pool.available_addrs.insert(free_ip);

        server.cache.insert(
            disc.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MIN,
            )?,
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_returns_available_requested_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();
        let requested_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        server.pool.available_addrs.insert(requested_ip);

        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        server.cache.insert(
            disc.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MIN,
            )?,
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, requested_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_expired_client_binding_returns_next_addr_for_unavailable_requested_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();
        let requested_ip = random_ipv4_generator();
        let free_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        server.pool.allocated_addrs.insert(requested_ip);
        server.pool.available_addrs.insert(free_ip);

        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        server.cache.insert(
            disc.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MIN,
            )?,
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_available_requested_addr_returns_requested_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();

        let requested_ip = random_ipv4_generator();
        let free_ip_1 = random_ipv4_generator();
        let free_ip_2 = random_ipv4_generator();

        server.pool.available_addrs.insert(free_ip_1);
        server.pool.available_addrs.insert(requested_ip);
        server.pool.available_addrs.insert(free_ip_2);

        // Update discover message to request for a specific ip
        // which is available in server pool.
        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, requested_ip);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_unavailable_requested_addr_returns_next_free_addr(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();

        let requested_ip = random_ipv4_generator();
        let free_ip_1 = random_ipv4_generator();

        server.pool.allocated_addrs.insert(requested_ip);
        server.pool.available_addrs.insert(free_ip_1);

        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip_1);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_unavailable_requested_addr_no_available_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut disc = new_test_discover();

        let requested_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(requested_ip);

        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        assert_eq!(
            server.dispatch(disc),
            Err(ServerError::ServerAddressPoolFailure(AddressPoolError::Ipv4AddrExhaustion))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_discover_no_requested_addr_no_available_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let disc = new_test_discover();
        server.pool.available_addrs.clear();

        assert_eq!(
            server.dispatch(disc),
            Err(ServerError::ServerAddressPoolFailure(AddressPoolError::Ipv4AddrExhaustion))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_offer_message_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;

        // Construct a simple offer sent by client.
        let mut client_offer = Message::new();
        client_offer.op = OpCode::BOOTREQUEST;
        client_offer.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPOFFER));

        assert_eq!(
            server.dispatch(client_offer),
            Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPOFFER))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_ack_message_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;

        // Construct a simple ack sent by client.
        let mut client_ack = Message::new();
        client_ack.op = OpCode::BOOTREQUEST;
        client_ack.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPACK));

        assert_eq!(
            server.dispatch(client_ack),
            Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPACK))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_nak_message_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;

        // Construct a simple nak sent by client.
        let mut client_nak = Message::new();
        client_nak.op = OpCode::BOOTREQUEST;
        client_nak.options.push(DhcpOption::DhcpMessageType(MessageType::DHCPNAK));

        assert_eq!(
            server.dispatch(client_nak),
            Err(ServerError::UnexpectedClientMessageType(MessageType::DHCPNAK))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_returns_correct_ack() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(requested_ip);

        // Update message to request for ip previously offered by server.
        req.ciaddr = requested_ip;

        let server_id = server.params.server_ips.first().unwrap();
        let router = match server.options_repo.get(&OptionCode::Router) {
            Some(DhcpOption::Router(router)) => Some(router.clone()),
            _ => None,
        }
        .ok_or(ProtocolError::MissingOption(OptionCode::Router))?;
        let dns_server = match server.options_repo.get(&OptionCode::DomainNameServer) {
            Some(DhcpOption::DomainNameServer(dns_server)) => Some(dns_server.clone()),
            _ => None,
        }
        .ok_or(ProtocolError::MissingOption(OptionCode::DomainNameServer))?;
        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                requested_ip,
                vec![
                    DhcpOption::ServerIdentifier(*server_id),
                    DhcpOption::IpAddressLeaseTime(server.params.lease_length.default_seconds),
                    DhcpOption::RenewalTimeValue(server.params.lease_length.default_seconds / 2),
                    DhcpOption::RebindingTimeValue(
                        (server.params.lease_length.default_seconds * 3) / 4,
                    ),
                    DhcpOption::SubnetMask(Ipv4Addr::new(255, 255, 255, 0)),
                    DhcpOption::Router(router),
                    DhcpOption::DomainNameServer(dns_server),
                ],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.ciaddr = requested_ip;
        expected_ack.yiaddr = requested_ip;

        let expected_dest = req.ciaddr;

        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_ack, Some(expected_dest)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_maintains_server_invariants() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();
        let client_mac = req.chaddr;

        server.pool.allocated_addrs.insert(requested_ip);
        req.ciaddr = requested_ip;
        server.cache.insert(
            client_mac,
            CachedConfig::new(requested_ip, vec![], std::time::SystemTime::now(), std::u32::MAX)?,
        );
        let _response = server.dispatch(req).unwrap();
        assert!(server.cache.contains_key(&client_mac));
        assert!(server.pool.addr_is_allocated(requested_ip));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_wrong_server_ip_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request_selecting_state(&server);

        // Update message to request for any ip.
        req.ciaddr = random_ipv4_generator();

        // Update request to have a server ip different from actual server ip.
        req.options.remove(req.options.len() - 1);
        req.options.push(DhcpOption::ServerIdentifier(random_ipv4_generator()));

        let server_ip =
            *server.params.server_ips.first().ok_or(ServerError::ServerMissingIpAddr)?;
        assert_eq!(server.dispatch(req), Err(ServerError::IncorrectDHCPServer(server_ip)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_unknown_client_mac_returns_error_maintains_server_invariants(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
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
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request_selecting_state(&server);

        let client_requested_ip = random_ipv4_generator();
        let server_offered_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(server_offered_ip);
        req.ciaddr = client_requested_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                server_offered_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
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
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(requested_ip);

        req.ciaddr = requested_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig::new(requested_ip, vec![], std::time::SystemTime::now(), std::u32::MIN)?,
        );

        assert_eq!(server.dispatch(req), Err(ServerError::ExpiredClientConfig));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_selecting_request_no_reserved_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request_selecting_state(&server);

        let requested_ip = random_ipv4_generator();

        req.ciaddr = requested_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig::new(requested_ip, vec![], std::time::SystemTime::now(), std::u32::MAX)?,
        );

        assert_eq!(server.dispatch(req), Err(ServerError::UnidentifiedRequestedIp(requested_ip)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_returns_correct_ack() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        // For init-reboot, server and requested ip must be on the same subnet.
        // Hard-coding ip values here to achieve that.
        let init_reboot_client_ip = Ipv4Addr::new(192, 168, 1, 60);
        server.params.server_ips = vec![Ipv4Addr::new(192, 168, 1, 1)];

        server.pool.allocated_addrs.insert(init_reboot_client_ip);

        // Update request to have the test requested ip.
        req.options.push(DhcpOption::RequestedIpAddress(init_reboot_client_ip));

        let server_id = server.params.server_ips.first().unwrap();
        let router = match server.options_repo.get(&OptionCode::Router) {
            Some(DhcpOption::Router(router)) => Some(router.clone()),
            _ => None,
        }
        .ok_or(ProtocolError::MissingOption(OptionCode::Router))?;
        let dns_server = match server.options_repo.get(&OptionCode::DomainNameServer) {
            Some(DhcpOption::DomainNameServer(dns_server)) => Some(dns_server.clone()),
            _ => None,
        }
        .ok_or(ProtocolError::MissingOption(OptionCode::DomainNameServer))?;
        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                init_reboot_client_ip,
                vec![
                    DhcpOption::ServerIdentifier(*server_id),
                    DhcpOption::IpAddressLeaseTime(server.params.lease_length.default_seconds),
                    DhcpOption::RenewalTimeValue(server.params.lease_length.default_seconds / 2),
                    DhcpOption::RebindingTimeValue(
                        (server.params.lease_length.default_seconds * 3) / 4,
                    ),
                    DhcpOption::SubnetMask(Ipv4Addr::new(255, 255, 255, 0)),
                    DhcpOption::Router(router),
                    DhcpOption::DomainNameServer(dns_server),
                ],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.yiaddr = init_reboot_client_ip;

        let expected_dest = req.yiaddr;

        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_ack, Some(expected_dest)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_client_on_wrong_subnet_returns_nak(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        // Update request to have requested ip not on same subnet as server.
        req.options.push(DhcpOption::RequestedIpAddress(random_ipv4_generator()));

        // The returned nak should be from this recipient server.
        let expected_nak =
            new_test_nak(&req, &server, "client and server are in different subnets".to_owned());
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, Some(Ipv4Addr::BROADCAST)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_with_giaddr_set_returns_nak_with_broadcast_bit_set(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();
        req.giaddr = random_ipv4_generator();

        // Update request to have requested ip not on same subnet as server,
        // to ensure we get a nak.
        req.options.push(DhcpOption::RequestedIpAddress(random_ipv4_generator()));

        let response = server.dispatch(req).unwrap();

        assert!(extract_message(response).bdcast_flag);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_unknown_client_mac_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        let client_mac = req.chaddr;

        // Update requested ip and server ip to be on the same subnet.
        req.options.push(DhcpOption::RequestedIpAddress(Ipv4Addr::new(192, 165, 30, 45)));
        server.params.server_ips = vec![Ipv4Addr::new(192, 165, 30, 1)];

        assert_eq!(server.dispatch(req), Err(ServerError::UnknownClientMac(client_mac)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_mismatched_requested_addr_returns_nak(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        // Update requested ip and server ip to be on the same subnet.
        let init_reboot_client_ip = Ipv4Addr::new(192, 165, 25, 4);
        req.options.push(DhcpOption::RequestedIpAddress(init_reboot_client_ip));
        server.params.server_ips = vec![Ipv4Addr::new(192, 165, 25, 1)];

        let server_cached_ip = Ipv4Addr::new(192, 165, 25, 10);
        server.pool.allocated_addrs.insert(server_cached_ip);
        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                server_cached_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
        );

        let expected_nak =
            new_test_nak(&req, &server, "requested ip is not assigned to client".to_owned());
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, Some(Ipv4Addr::BROADCAST)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_expired_client_binding_returns_nak(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        let init_reboot_client_ip = Ipv4Addr::new(192, 165, 25, 4);
        req.options.push(DhcpOption::RequestedIpAddress(init_reboot_client_ip));
        server.params.server_ips = vec![Ipv4Addr::new(192, 165, 25, 1)];

        server.pool.allocated_addrs.insert(init_reboot_client_ip);
        // Expire client binding to make it invalid.
        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                init_reboot_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MIN,
            )?,
        );

        let expected_nak =
            new_test_nak(&req, &server, "requested ip is not assigned to client".to_owned());

        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, Some(Ipv4Addr::BROADCAST)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_init_boot_request_no_reserved_addr_returns_nak() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        let init_reboot_client_ip = Ipv4Addr::new(192, 165, 25, 4);
        req.options.push(DhcpOption::RequestedIpAddress(init_reboot_client_ip));
        server.params.server_ips = vec![Ipv4Addr::new(192, 165, 25, 1)];

        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                init_reboot_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
        );

        let expected_nak =
            new_test_nak(&req, &server, "requested ip is not assigned to client".to_owned());

        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, Some(Ipv4Addr::BROADCAST)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_returns_correct_ack() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        req.ciaddr = bound_client_ip;

        let server_id = server.params.server_ips.first().unwrap();
        let router = match server.options_repo.get(&OptionCode::Router) {
            Some(DhcpOption::Router(router)) => Some(router.clone()),
            _ => None,
        }
        .ok_or(ProtocolError::MissingOption(OptionCode::Router))?;
        let dns_server = match server.options_repo.get(&OptionCode::DomainNameServer) {
            Some(DhcpOption::DomainNameServer(dns_server)) => Some(dns_server.clone()),
            _ => None,
        }
        .ok_or(ProtocolError::MissingOption(OptionCode::DomainNameServer))?;
        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![
                    DhcpOption::ServerIdentifier(*server_id),
                    DhcpOption::IpAddressLeaseTime(server.params.lease_length.default_seconds),
                    DhcpOption::RenewalTimeValue(server.params.lease_length.default_seconds / 2),
                    DhcpOption::RebindingTimeValue(
                        (server.params.lease_length.default_seconds * 3) / 4,
                    ),
                    DhcpOption::SubnetMask(Ipv4Addr::new(255, 255, 255, 0)),
                    DhcpOption::Router(router),
                    DhcpOption::DomainNameServer(dns_server),
                ],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.yiaddr = bound_client_ip;
        expected_ack.ciaddr = bound_client_ip;

        let expected_dest = req.ciaddr;

        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_ack, Some(expected_dest)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_unknown_client_mac_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
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
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        let client_renewal_ip = random_ipv4_generator();
        let bound_client_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        req.ciaddr = client_renewal_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
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
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();

        server.pool.allocated_addrs.insert(bound_client_ip);
        req.ciaddr = bound_client_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MIN,
            )?,
        );

        assert_eq!(server.dispatch(req), Err(ServerError::ExpiredClientConfig));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_renewing_request_no_reserved_addr_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();
        req.ciaddr = bound_client_ip;

        server.cache.insert(
            req.chaddr,
            CachedConfig::new(
                bound_client_ip,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
        );

        assert_eq!(
            server.dispatch(req),
            Err(ServerError::UnidentifiedRequestedIp(bound_client_ip))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_unknown_client_state_returns_error() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;

        let req = new_test_request();

        assert_eq!(server.dispatch(req), Err(ServerError::UnknownClientStateDuringRequest));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_client_state_with_selecting_returns_selecting() -> Result<(), Error> {
        let mut req = new_test_request();

        // Selecting state request must have server id and ciaddr populated.
        req.ciaddr = random_ipv4_generator();
        req.options.push(DhcpOption::ServerIdentifier(random_ipv4_generator()));

        assert_eq!(get_client_state(&req), Ok(ClientState::Selecting));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_client_state_with_initreboot_returns_initreboot() -> Result<(), Error> {
        let mut req = new_test_request();

        // Init reboot state request must have requested ip populated.
        req.options.push(DhcpOption::RequestedIpAddress(random_ipv4_generator()));

        assert_eq!(get_client_state(&req), Ok(ClientState::InitReboot));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_client_state_with_renewing_returns_renewing() -> Result<(), Error> {
        let mut req = new_test_request();

        // Renewing state request must have ciaddr populated.
        req.ciaddr = random_ipv4_generator();

        assert_eq!(get_client_state(&req), Ok(ClientState::Renewing));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_client_state_with_unknown_returns_unknown() -> Result<(), Error> {
        let msg = new_test_request();

        assert_eq!(get_client_state(&msg), Err(()));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_client_msg_missing_message_type_option_returns_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mut msg = new_test_request();
        msg.options.clear();

        assert_eq!(
            server.dispatch(msg),
            Err(ServerError::ClientMessageError(ProtocolError::MissingOption(
                OptionCode::DhcpMessageType
            )))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_release_expired_leases_with_none_expired_releases_none() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        server.pool.available_addrs.clear();

        // Insert client 1 bindings.
        let client_1_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_1_ip);
        server.store_client_config(
            client_1_ip,
            random_mac_generator(),
            &[DhcpOption::IpAddressLeaseTime(std::u32::MAX)],
        )?;

        // Insert client 2 bindings.
        let client_2_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_2_ip);
        server.store_client_config(
            client_2_ip,
            random_mac_generator(),
            &[DhcpOption::IpAddressLeaseTime(std::u32::MAX)],
        )?;

        // Insert client 3 bindings.
        let client_3_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_3_ip);
        server.store_client_config(
            client_3_ip,
            random_mac_generator(),
            &[DhcpOption::IpAddressLeaseTime(std::u32::MAX)],
        )?;

        let () = server.release_expired_leases()?;

        assert_eq!(server.cache.len(), 3);
        assert_eq!(server.pool.available_addrs.len(), 0);
        assert_eq!(server.pool.allocated_addrs.len(), 3);
        let keys = get_keys(&mut server).await.context("failed to get keys")?;
        assert_eq!(keys.len(), 3);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_release_expired_leases_with_all_expired_releases_all() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        server.pool.available_addrs.clear();

        let client_1_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_1_ip);
        let () = server.store_client_config(
            client_1_ip,
            random_mac_generator(),
            &[DhcpOption::IpAddressLeaseTime(0)],
        )?;

        let client_2_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_2_ip);
        let () = server.store_client_config(
            client_2_ip,
            random_mac_generator(),
            &[DhcpOption::IpAddressLeaseTime(0)],
        )?;

        let client_3_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_3_ip);
        let () = server.store_client_config(
            client_3_ip,
            random_mac_generator(),
            &[DhcpOption::IpAddressLeaseTime(0)],
        )?;

        let () = server.release_expired_leases()?;

        assert_eq!(server.cache.len(), 0);
        assert_eq!(server.pool.available_addrs.len(), 3);
        assert_eq!(server.pool.allocated_addrs.len(), 0);
        let keys = get_keys(&mut server).await.context("failed to get keys")?;
        assert_eq!(keys.len(), 0);
        Ok(())
    }

    async fn get_keys(server: &mut Server) -> Result<Vec<fidl_fuchsia_stash::KeyValue>, Error> {
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
        let mut server = new_test_minimal_server().await?;
        server.pool.available_addrs.clear();

        let client_1_mac = random_mac_generator();
        let client_1_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_1_ip);
        let () = server.store_client_config(
            client_1_ip,
            client_1_mac,
            &[DhcpOption::IpAddressLeaseTime(std::u32::MAX)],
        )?;

        let client_2_mac = random_mac_generator();
        let client_2_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_2_ip);
        let () = server.store_client_config(
            client_2_ip,
            client_2_mac,
            &[DhcpOption::IpAddressLeaseTime(0)],
        )?;

        let client_3_mac = random_mac_generator();
        let client_3_ip = random_ipv4_generator();
        server.pool.available_addrs.insert(client_3_ip);
        let () = server.store_client_config(
            client_3_ip,
            client_3_mac,
            &[DhcpOption::IpAddressLeaseTime(std::u32::MAX)],
        )?;

        let () = server.release_expired_leases()?;

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
        let mut server = new_test_minimal_server().await?;
        let mut release = new_test_release();

        let release_ip = random_ipv4_generator();
        let client_mac = release.chaddr;

        server.pool.allocated_addrs.insert(release_ip);
        release.ciaddr = release_ip;

        let test_client_config = || {
            CachedConfig::new(release_ip, vec![], std::time::SystemTime::now(), std::u32::MAX)
                .unwrap()
        };

        server.cache.insert(client_mac, test_client_config());

        assert_eq!(server.dispatch(release), Ok(ServerAction::AddressRelease(release_ip)));

        assert!(!server.pool.addr_is_allocated(release_ip), "addr marked allocated");
        assert!(server.pool.addr_is_available(release_ip), "addr not marked available");
        assert!(server.cache.contains_key(&client_mac), "client config not retained");
        assert_eq!(
            server.cache.get(&client_mac).unwrap(),
            &test_client_config(),
            "retained client config changed"
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_unknown_release_maintains_server_state_returns_unknown_mac_error(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
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
        let mut server = new_test_minimal_server().await?;
        let mut inform = new_test_inform();

        let inform_client_ip = random_ipv4_generator();

        inform.ciaddr = inform_client_ip;

        let mut expected_ack = new_test_inform_ack(&inform, &server);
        expected_ack.ciaddr = inform_client_ip;

        let expected_dest = inform.ciaddr;

        assert_eq!(
            server.dispatch(inform),
            Ok(ServerAction::SendResponse(expected_ack, Some(expected_dest)))
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dispatch_with_decline_for_valid_client_binding_updates_cache() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server().await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        server.pool.allocated_addrs.insert(declined_ip);

        server.cache.insert(
            client_mac,
            CachedConfig::new(declined_ip, vec![], std::time::SystemTime::now(), std::u32::MAX)?,
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
        let mut server = new_test_minimal_server().await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

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
            CachedConfig::new(
                client_ip_according_to_server,
                vec![],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
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
        let mut server = new_test_minimal_server().await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        server.pool.available_addrs.insert(declined_ip);

        server.cache.insert(
            client_mac,
            CachedConfig::new(declined_ip, vec![], std::time::SystemTime::now(), std::u32::MIN)?,
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
        let mut server = new_test_minimal_server().await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        // Server contains client bindings which reflect a different address
        // than the one being declined.
        server.cache.insert(
            client_mac,
            CachedConfig::new(
                random_ipv4_generator(),
                vec![],
                std::time::SystemTime::now(),
                std::u32::MAX,
            )?,
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
        let mut server = new_test_minimal_server().await?;
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        server.pool.available_addrs.insert(declined_ip);

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));

        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    // TODO(fxbug.dev/21422): Revisit when decline behavior is verified.
    async fn test_dispatch_with_decline_for_incorrect_server_recepient_deletes_client_binding(
    ) -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        server.params.server_ips = vec![Ipv4Addr::new(192, 168, 1, 1)];

        let mut decline = new_test_decline(&server);

        // Updating decline request to have wrong server ip.
        decline.options.remove(1);
        decline.options.push(DhcpOption::ServerIdentifier(Ipv4Addr::new(1, 2, 3, 4)));

        let declined_ip = random_ipv4_generator();
        let client_mac = decline.chaddr;

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        server.pool.allocated_addrs.insert(declined_ip);
        server.cache.insert(
            client_mac,
            CachedConfig::new(declined_ip, vec![], std::time::SystemTime::now(), std::u32::MAX)?,
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
        let mut server = new_test_minimal_server().await?;
        let decline = new_test_decline(&server);

        assert_eq!(server.dispatch(decline), Err(ServerError::NoRequestedAddrForDecline));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_client_requested_lease_time() -> Result<(), Error> {
        let mut disc = new_test_discover();
        let client_mac = disc.chaddr;

        let client_requested_time: u32 = 20;

        disc.options.push(DhcpOption::IpAddressLeaseTime(client_requested_time));

        let mut server = new_test_minimal_server().await?;
        server.pool.available_addrs.insert(random_ipv4_generator());

        let response = server.dispatch(disc).unwrap();
        assert_eq!(
            extract_message(response)
                .options
                .iter()
                .filter_map(|opt| {
                    if let DhcpOption::IpAddressLeaseTime(v) = opt {
                        Some(*v)
                    } else {
                        None
                    }
                })
                .next()
                .unwrap(),
            client_requested_time as u32
        );

        assert_eq!(
            server.cache.get(&client_mac).unwrap().lease_length_seconds,
            client_requested_time,
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_client_requested_lease_time_greater_than_max() -> Result<(), Error> {
        let mut disc = new_test_discover();
        let client_mac = disc.chaddr;

        let client_requested_time: u32 = 20;
        let server_max_lease_time: u32 = 10;

        disc.options.push(DhcpOption::IpAddressLeaseTime(client_requested_time));

        let mut server = new_test_minimal_server().await?;
        server.pool.available_addrs.insert(Ipv4Addr::new(195, 168, 1, 45));
        let ll = LeaseLength { default_seconds: 60 * 60 * 24, max_seconds: server_max_lease_time };
        server.params.lease_length = ll;

        let response = server.dispatch(disc).unwrap();
        assert_eq!(
            extract_message(response)
                .options
                .iter()
                .filter_map(|opt| {
                    if let DhcpOption::IpAddressLeaseTime(v) = opt {
                        Some(*v)
                    } else {
                        None
                    }
                })
                .next()
                .unwrap(),
            server_max_lease_time
        );

        assert_eq!(
            server.cache.get(&client_mac).unwrap().lease_length_seconds,
            server_max_lease_time,
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_server_dispatcher_get_option_with_unset_option_returns_not_found(
    ) -> Result<(), Error> {
        let server = new_test_minimal_server().await?;
        let result = server.dispatch_get_option(fidl_fuchsia_net_dhcp::OptionCode::SubnetMask);
        assert_eq!(result, Err(Status::NOT_FOUND));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_server_dispatcher_get_option_with_set_option_returns_option() -> Result<(), Error>
    {
        let mut server = new_test_minimal_server().await?;
        let option = || {
            fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_fuchsia_net::Ipv4Address {
                addr: [255, 255, 255, 0],
            })
        };
        server.options_repo.insert(OptionCode::SubnetMask, DhcpOption::try_from_fidl(option())?);
        let result = server.dispatch_get_option(fidl_fuchsia_net_dhcp::OptionCode::SubnetMask)?;
        assert_eq!(result, option());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_server_dispatcher_set_option_returns_unit() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let option = || {
            fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_fuchsia_net::Ipv4Address {
                addr: [255, 255, 255, 0],
            })
        };
        let () = server.dispatch_set_option(option())?;
        let stored_option: DhcpOption = DhcpOption::try_from_fidl(option())?;
        let code = stored_option.code();
        let result = server.options_repo.get(&code);
        assert_eq!(result, Some(&stored_option));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_server_dispatcher_list_options_returns_set_options() -> Result<(), Error> {
        let mut server = new_test_minimal_server().await?;
        let mask = || {
            fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_fuchsia_net::Ipv4Address {
                addr: [255, 255, 255, 0],
            })
        };
        let hostname = || fidl_fuchsia_net_dhcp::Option_::HostName(String::from("testhostname"));
        server.options_repo.insert(OptionCode::SubnetMask, DhcpOption::try_from_fidl(mask())?);
        server.options_repo.insert(OptionCode::HostName, DhcpOption::try_from_fidl(hostname())?);
        let result = server.dispatch_list_options()?;
        assert_eq!(result.len(), server.options_repo.len());
        assert!(result.contains(&mask()));
        assert!(result.contains(&hostname()));
        Ok(())
    }
}
