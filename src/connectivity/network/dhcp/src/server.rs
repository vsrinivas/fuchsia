// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::configuration::ServerParameters;
use crate::protocol::{
    identifier::ClientIdentifier, DhcpOption, FidlCompatible, FromFidlExt, IntoFidlExt, Message,
    MessageType, OpCode, OptionCode, ProtocolError,
};
use anyhow::{Context as _, Error};
use fuchsia_zircon::Status;
use net_types::ethernet::Mac as MacAddr;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeSet, HashMap};
use std::convert::TryFrom;
use std::net::Ipv4Addr;
use thiserror::Error;

/// A minimal DHCP server.
///
/// This comment will be expanded upon in future CLs as the server design
/// is iterated upon.
pub struct Server<DS: DataStore, TS: SystemTimeSource = StdSystemTime> {
    records: ClientRecords,
    pool: AddressPool,
    params: ServerParameters,
    store: Option<DS>,
    options_repo: HashMap<OptionCode, DhcpOption>,
    time_source: TS,
}

// An interface for Server to retrieve the current time.
pub trait SystemTimeSource {
    fn with_current_time() -> Self;
    fn now(&self) -> std::time::SystemTime;
}

// SystemTimeSource that uses std::time::SystemTime::now().
pub struct StdSystemTime;

impl SystemTimeSource for StdSystemTime {
    fn with_current_time() -> Self {
        StdSystemTime
    }

    fn now(&self) -> std::time::SystemTime {
        std::time::SystemTime::now()
    }
}

/// An interface for storing and loading DHCP server data.
pub trait DataStore {
    type Error: std::error::Error + std::marker::Send + std::marker::Sync + 'static;

    /// Inserts the client record associated with the identifier.
    fn insert(
        &mut self,
        client_id: &ClientIdentifier,
        record: &LeaseRecord,
    ) -> Result<(), Self::Error>;

    /// Stores the DHCP option values served by the server.
    fn store_options(&mut self, opts: &[DhcpOption]) -> Result<(), Self::Error>;

    /// Stores the DHCP server's configuration parameters.
    fn store_parameters(&mut self, params: &ServerParameters) -> Result<(), Self::Error>;

    /// Deletes the client record associated with the identifier.
    fn delete(&mut self, client_id: &ClientIdentifier) -> Result<(), Self::Error>;
}

/// The default string used by the Server to identify itself to the Stash service.
pub const DEFAULT_STASH_ID: &str = "dhcpd";

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
    SendResponse(Message, ResponseTarget),
    AddressDecline(Ipv4Addr),
    AddressRelease(Ipv4Addr),
}

/// The destinations to which a response can be targeted. A `Broadcast`
/// will be targeted to the IPv4 Broadcast address. A `Unicast` will be
/// targeted to its `Ipv4Addr` associated value. If a `MacAddr` is supplied,
/// the target may not yet have the `Ipv4Addr` assigned, so the response
/// should be manually directed to the `MacAddr`, typically by updating the
/// ARP cache.
#[derive(Debug, PartialEq)]
pub enum ResponseTarget {
    Broadcast,
    Unicast(Ipv4Addr, Option<MacAddr>),
}

/// A wrapper around the error types which can be returned by DHCP Server
/// in response to client requests.
/// Implements `PartialEq` for test assertions.
#[derive(Debug, Error, PartialEq)]
pub enum ServerError {
    #[error("unexpected client message type: {}", _0)]
    UnexpectedClientMessageType(MessageType),

    #[error("requested ip parsing failure: {}", _0)]
    BadRequestedIpv4Addr(String),

    #[error("local address pool manipulation error: {}", _0)]
    ServerAddressPoolFailure(AddressPoolError),

    #[error("incorrect server ip in client message: {}", _0)]
    IncorrectDHCPServer(Ipv4Addr),

    #[error("requested ip mismatch with offered ip: {} {}", _0, _1)]
    RequestedIpOfferIpMismatch(Ipv4Addr, Ipv4Addr),

    #[error("expired client lease record")]
    ExpiredLeaseRecord,

    #[error("requested ip absent from server pool: {}", _0)]
    UnidentifiedRequestedIp(Ipv4Addr),

    #[error("unknown client identifier: {}", _0)]
    UnknownClientId(ClientIdentifier),

    #[error("init reboot request did not include ip")]
    NoRequestedAddrAtInitReboot,

    #[error("unidentified client state during request")]
    UnknownClientStateDuringRequest,

    #[error("decline request did not include ip")]
    NoRequestedAddrForDecline,

    #[error("client request error: {}", _0)]
    ClientMessageError(ProtocolError),

    #[error("error manipulating server data store: {}", _0)]
    DataStoreUpdateFailure(DataStoreError),

    #[error("server not configured with an ip address")]
    ServerMissingIpAddr,

    #[error("missing required dhcp option: {:?}", _0)]
    MissingRequiredDhcpOption(OptionCode),

    #[error("missing server identifier in response")]
    // According to RFC 2131, page 28, all server responses MUST include server identifier.
    //
    // https://tools.ietf.org/html/rfc2131#page-29
    MissingServerIdentifier,

    #[error("unable to get system time")]
    // The underlying error is not provided to this variant as it (std::time::SystemTimeError) does
    // not implement PartialEq.
    ServerTimeError,

    #[error("inconsistent initial server state: {}", _0)]
    InconsistentInitialServerState(AddressPoolError),

    #[error("client request message missing requested ip addr")]
    MissingRequestedAddr,

    #[error("decline from unrecognized client: {:?}", _0)]
    DeclineFromUnrecognizedClient(ClientIdentifier),

    #[error(
        "declined ip mismatched with lease: got declined addr {:?}, want client addr {:?}",
        declined,
        client
    )]
    DeclineIpMismatch { declined: Option<Ipv4Addr>, client: Option<Ipv4Addr> },
}

impl From<AddressPoolError> for ServerError {
    fn from(e: AddressPoolError) -> Self {
        ServerError::ServerAddressPoolFailure(e)
    }
}

/// This struct is used to hold the error returned by the server's
/// DataStore manipulation methods. We manually implement `PartialEq` so this
/// struct could be included in the `ServerError` enum,
/// which are asserted for equality in tests.
#[derive(Debug, Error)]
#[error(transparent)]
pub struct DataStoreError(#[from] anyhow::Error);

impl PartialEq for DataStoreError {
    fn eq(&self, _other: &Self) -> bool {
        false
    }
}

impl<DS: DataStore, TS: SystemTimeSource> Server<DS, TS> {
    /// Attempts to instantiate a new `Server` value from the persisted state contained in the
    /// provided parts. If the client leases and address pool contained in the provided parts are
    /// inconsistent with one another, then instantiation will fail.
    pub fn new_from_state(
        store: DS,
        params: ServerParameters,
        options_repo: HashMap<OptionCode, DhcpOption>,
        records: ClientRecords,
    ) -> Result<Self, Error> {
        Self::new_with_time_source(store, params, options_repo, records, TS::with_current_time())
    }

    pub fn new_with_time_source(
        store: DS,
        params: ServerParameters,
        options_repo: HashMap<OptionCode, DhcpOption>,
        records: ClientRecords,
        time_source: TS,
    ) -> Result<Self, Error> {
        let mut pool = AddressPool::new(params.managed_addrs.pool_range());
        for client_addr in records.iter().filter_map(|(_id, LeaseRecord { current, .. })| *current)
        {
            let () = pool
                .allocate_addr(client_addr)
                .map_err(ServerError::InconsistentInitialServerState)?;
        }
        let mut server =
            Self { records, pool, params, store: Some(store), options_repo, time_source };
        let () = server.release_expired_leases()?;
        Ok(server)
    }

    /// Instantiates a new `Server`, without persisted state, from the supplied parameters.
    pub fn new(store: Option<DS>, params: ServerParameters) -> Self {
        Self {
            records: HashMap::new(),
            pool: AddressPool::new(params.managed_addrs.pool_range()),
            params,
            store,
            options_repo: HashMap::new(),
            time_source: TS::with_current_time(),
        }
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
    fn get_destination(&mut self, client_msg: &Message, offered: Ipv4Addr) -> ResponseTarget {
        if !client_msg.giaddr.is_unspecified() {
            ResponseTarget::Unicast(client_msg.giaddr, None)
        } else if !client_msg.ciaddr.is_unspecified() {
            ResponseTarget::Unicast(client_msg.ciaddr, None)
        } else if client_msg.bdcast_flag {
            ResponseTarget::Broadcast
        } else {
            ResponseTarget::Unicast(offered, Some(client_msg.chaddr))
        }
    }

    fn handle_discover(&mut self, disc: Message) -> Result<ServerAction, ServerError> {
        let () = validate_discover(&disc)?;
        let client_id = ClientIdentifier::from(&disc);
        let offered = self.get_offered(&disc)?;
        let dest = self.get_destination(&disc, offered);
        let offer = self.build_offer(disc, offered)?;
        match self.store_client_record(offered, client_id, &offer.options) {
            Ok(()) => Ok(ServerAction::SendResponse(offer, dest)),
            Err(e) => Err(ServerError::DataStoreUpdateFailure(e.into())),
        }
    }

    // Determine the address to offer to the client.
    //
    // This function follows the address offer algorithm in
    // https://tools.ietf.org/html/rfc2131#section-4.3.1:
    //
    // If an address is available, the new address SHOULD be chosen as follows:
    //
    // o The client's current address as recorded in the client's current
    // binding, ELSE
    //
    // o The client's previous address as recorded in the client's (now
    // expired or released) binding, if that address is in the server's
    // pool of available addresses and not already allocated, ELSE
    //
    // o The address requested in the 'Requested IP Address' option, if that
    // address is valid and not already allocated, ELSE
    //
    // o A new address allocated from the server's pool of available
    // addresses; the address is selected based on the subnet from which
    // the message was received (if 'giaddr' is 0) or on the address of
    // the relay agent that forwarded the message ('giaddr' when not 0).
    fn get_offered(&mut self, client: &Message) -> Result<Ipv4Addr, ServerError> {
        let id = ClientIdentifier::from(client);
        if let Some(LeaseRecord { current, previous, .. }) = self.records.get(&id) {
            if let Some(current) = current {
                if !self.pool.addr_is_allocated(*current) {
                    panic!("address {} from active lease is unallocated in address pool", current);
                }
                return Ok(*current);
            }
            if let Some(previous) = previous {
                if self.pool.addr_is_available(*previous) {
                    return Ok(*previous);
                }
            }
        }
        if let Some(requested_addr) = get_requested_ip_addr(&client) {
            if self.pool.addr_is_available(requested_addr) {
                return Ok(requested_addr);
            }
        }
        // TODO(https://fxbug.dev/21423): The ip should be handed out based on
        // client subnet. Currently, the server blindly hands out the next
        // available ip from its available ip pool, without any subnet analysis.
        if let Some(addr) = self.pool.available().next() {
            return Ok(addr);
        }
        let () = self.release_expired_leases()?;
        if let Some(addr) = self.pool.available().next() {
            return Ok(addr);
        }
        Err(ServerError::ServerAddressPoolFailure(AddressPoolError::Ipv4AddrExhaustion))
    }

    fn store_client_record(
        &mut self,
        offered: Ipv4Addr,
        client_id: ClientIdentifier,
        client_opts: &[DhcpOption],
    ) -> Result<(), Error> {
        let lease_length_seconds = client_opts
            .iter()
            .find_map(|opt| match opt {
                DhcpOption::IpAddressLeaseTime(v) => Some(*v),
                _ => None,
            })
            .ok_or(ServerError::MissingRequiredDhcpOption(OptionCode::IpAddressLeaseTime))?;
        let options = client_opts
            .iter()
            .filter(|opt| {
                // DhcpMessageType is part of transaction semantics and should not be stored.
                opt.code() != OptionCode::DhcpMessageType
            })
            .cloned()
            .collect();
        let record =
            LeaseRecord::new(Some(offered), options, self.time_source.now(), lease_length_seconds)?;
        let Self { records, pool, store, .. } = self;
        let entry = records.entry(client_id);
        let current = match &entry {
            std::collections::hash_map::Entry::Occupied(occupied) => {
                let LeaseRecord { current, .. } = occupied.get();
                *current
            }
            std::collections::hash_map::Entry::Vacant(_vacant) => None,
        };
        let () = match current {
            Some(current) => {
                // If there is a lease record with a currently leased address, and the offered address
                // does not match that leased address, then the offered address was calculated contrary
                // to RFC2131#section4.3.1.
                assert_eq!(
                    current, offered,
                    "server offered address does not match address in lease record"
                );
            }
            None => {
                match pool.allocate_addr(offered) {
                    Ok(()) => (),
                    // An error here indicates that the offered address is already allocated, an
                    // irrecoverable inconsistency.
                    Err(e) => panic!("fatal server address allocation failure: {}", e),
                }
            }
        };
        if let Some(store) = store {
            let () =
                store.insert(entry.key(), &record).context("failed to store client in stash")?;
        }
        // TODO(https://github.com/rust-lang/rust/issues/65225): use entry.insert.
        let () = match entry {
            std::collections::hash_map::Entry::Occupied(mut occupied) => {
                let _: LeaseRecord = occupied.insert(record);
            }
            std::collections::hash_map::Entry::Vacant(vacant) => {
                let _: &mut LeaseRecord = vacant.insert(record);
            }
        };
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
        let requested_ip = get_requested_ip_addr(&req)
            .ok_or(ServerError::MissingRequiredDhcpOption(OptionCode::RequestedIpAddress))?;
        if !is_recipient(&self.params.server_ips, &req) {
            Err(ServerError::IncorrectDHCPServer(
                *self.params.server_ips.first().ok_or(ServerError::ServerMissingIpAddr)?,
            ))
        } else {
            self.build_response(req, requested_ip)
        }
    }

    fn build_response(
        &mut self,
        req: Message,
        requested_ip: Ipv4Addr,
    ) -> Result<ServerAction, ServerError> {
        match self.validate_requested_addr_with_client(&req, requested_ip) {
            Ok(()) => {
                let dest = self.get_destination(&req, requested_ip);
                Ok(ServerAction::SendResponse(self.build_ack(req, requested_ip)?, dest))
            }
            Err(e) => {
                let (nak, dest) = self.build_nak(req, NakReason::ClientValidationFailure(e))?;
                Ok(ServerAction::SendResponse(nak, dest))
            }
        }
    }

    /// The function below validates if the `requested_ip` is correctly
    /// associated with the client whose request `req` is being processed.
    ///
    /// It first checks if the client bindings can be found in server records.
    /// If not, the association is wrong and it returns an `Err()`.
    ///
    /// If the server can correctly locate the client bindings in its records,
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
        let client_id = ClientIdentifier::from(req);
        if let Some(record) = self.records.get(&client_id) {
            let now = self
                .time_source
                .now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_err(|std::time::SystemTimeError { .. }| ServerError::ServerTimeError)?;
            if let Some(client_addr) = record.current {
                if client_addr != requested_ip {
                    Err(ServerError::RequestedIpOfferIpMismatch(requested_ip, client_addr))
                } else if record.expired(now) {
                    Err(ServerError::ExpiredLeaseRecord)
                } else if !self.pool.addr_is_allocated(requested_ip) {
                    Err(ServerError::UnidentifiedRequestedIp(requested_ip))
                } else {
                    Ok(())
                }
            } else {
                Err(ServerError::MissingRequestedAddr)
            }
        } else {
            Err(ServerError::UnknownClientId(client_id))
        }
    }

    fn handle_request_init_reboot(&mut self, req: Message) -> Result<ServerAction, ServerError> {
        let requested_ip =
            get_requested_ip_addr(&req).ok_or(ServerError::NoRequestedAddrAtInitReboot)?;
        if !is_in_subnet(&req, &self.params) {
            let (nak, dest) = self.build_nak(req, NakReason::DifferentSubnets)?;
            return Ok(ServerAction::SendResponse(nak, dest));
        }
        let client_id = ClientIdentifier::from(&req);
        if !self.records.contains_key(&client_id) {
            return Err(ServerError::UnknownClientId(client_id));
        }
        self.build_response(req, requested_ip)
    }

    fn handle_request_renewing(&mut self, req: Message) -> Result<ServerAction, ServerError> {
        let client_ip = req.ciaddr;
        self.build_response(req, client_ip)
    }

    // RFC 2131 provides limited guidance for implementation of DHCPDECLINE handling. From
    // https://tools.ietf.org/html/rfc2131#section-4.3.3:
    //
    //   If the server receives a DHCPDECLINE message... The server MUST mark the network address
    //   as not available...
    //
    // However, the RFC does not specify what a valid DHCPDECLINE message looks like. If all
    // DHCPDECLINE messages are acted upon, then the server will be exposed to DoS attacks.
    //
    // We define a valid DHCPDECLINE message as:
    //   * ServerIdentifier matches the server
    //   * server has a record of a lease to the client
    //   * the declined IP matches the leased IP
    //
    // Only if those three conditions obtain, the server will then invalidate the lease and mark
    // the address as allocated and unavailable for assignment (if it isn't already).
    fn handle_decline(&mut self, dec: Message) -> Result<ServerAction, ServerError> {
        let Self { records, params, pool, store, .. } = self;
        let declined_ip =
            get_requested_ip_addr(&dec).ok_or_else(|| ServerError::NoRequestedAddrForDecline)?;
        let id = ClientIdentifier::from(&dec);
        if !is_recipient(&params.server_ips, &dec) {
            return Err(ServerError::IncorrectDHCPServer(
                get_server_id_from(&dec).ok_or(ServerError::MissingServerIdentifier)?,
            ));
        }
        let entry = match records.entry(id) {
            std::collections::hash_map::Entry::Occupied(v) => v,
            std::collections::hash_map::Entry::Vacant(v) => {
                return Err(ServerError::DeclineFromUnrecognizedClient(v.into_key()))
            }
        };
        let LeaseRecord { current, .. } = entry.get();
        if *current != Some(declined_ip) {
            return Err(ServerError::DeclineIpMismatch {
                declined: Some(declined_ip),
                client: *current,
            });
        }
        // The declined address must be marked allocated/unavailable. Depending on whether the
        // client declines the address after an OFFER or an ACK, a declined address may already be
        // marked allocated. Attempt to allocate the declined address, but treat the address
        // already being allocated as success.
        let () = pool.allocate_addr(declined_ip).or_else(|e| match e {
            AddressPoolError::AllocatedIpv4AddrAllocation(ip) if ip == declined_ip => Ok(()),
            e @ AddressPoolError::Ipv4AddrExhaustion
            | e @ AddressPoolError::AllocatedIpv4AddrAllocation(Ipv4Addr { .. })
            | e @ AddressPoolError::UnallocatedIpv4AddrRelease(Ipv4Addr { .. })
            | e @ AddressPoolError::UnmanagedIpv4Addr(Ipv4Addr { .. }) => Err(e),
        })?;
        let (id, LeaseRecord { .. }) = entry.remove_entry();
        if let Some(store) = store {
            let () = store
                .delete(&id)
                .map_err(|e| ServerError::DataStoreUpdateFailure(anyhow::Error::from(e).into()))?;
        }
        Ok(ServerAction::AddressDecline(declined_ip))
    }

    fn handle_release(&mut self, rel: Message) -> Result<ServerAction, ServerError> {
        let Self { records, pool, store, .. } = self;
        let client_id = ClientIdentifier::from(&rel);
        if let Some(record) = records.get_mut(&client_id) {
            // From https://tools.ietf.org/html/rfc2131#section-4.3.4:
            //
            // Upon receipt of a DHCPRELEASE message, the server marks the network address as not
            // allocated.  The server SHOULD retain a record of the client's initialization
            // parameters for possible reuse in response to subsequent requests from the client.
            let () = release_leased_addr(&client_id, record, pool, store)?;
            Ok(ServerAction::AddressRelease(rel.ciaddr))
        } else {
            Err(ServerError::UnknownClientId(client_id))
        }
    }

    fn handle_inform(&mut self, inf: Message) -> Result<ServerAction, ServerError> {
        // When responding to an INFORM, the server must leave yiaddr zeroed.
        let yiaddr = Ipv4Addr::UNSPECIFIED;
        let dest = self.get_destination(&inf, inf.ciaddr);
        let ack = self.build_inform_ack(inf, yiaddr)?;
        Ok(ServerAction::SendResponse(ack, dest))
    }

    fn build_offer(&self, disc: Message, offered_ip: Ipv4Addr) -> Result<Message, ServerError> {
        let mut options = Vec::new();
        options.push(DhcpOption::DhcpMessageType(MessageType::DHCPOFFER));
        options.push(DhcpOption::ServerIdentifier(self.get_server_ip(&disc)?));
        let lease_length = match disc.options.iter().find_map(|opt| match opt {
            DhcpOption::IpAddressLeaseTime(seconds) => Some(*seconds),
            _ => None,
        }) {
            Some(seconds) => std::cmp::min(seconds, self.params.lease_length.max_seconds),
            None => self.params.lease_length.default_seconds,
        };
        options.push(DhcpOption::IpAddressLeaseTime(lease_length));
        let v = self
            .options_repo
            .get(&OptionCode::RenewalTimeValue)
            .map(|v| match v {
                DhcpOption::RenewalTimeValue(v) => *v,
                v => panic!(
                    "options repo contains code-value mismatch: key={:?} value={:?}",
                    &OptionCode::RenewalTimeValue,
                    v
                ),
            })
            .unwrap_or_else(|| lease_length / 2);
        options.push(DhcpOption::RenewalTimeValue(v));
        let v = self
            .options_repo
            .get(&OptionCode::RebindingTimeValue)
            .map(|v| match v {
                DhcpOption::RebindingTimeValue(v) => *v,
                v => panic!(
                    "options repo contains code-value mismatch: key={:?} value={:?}",
                    &OptionCode::RenewalTimeValue,
                    v
                ),
            })
            .unwrap_or_else(|| (lease_length / 4) * 3 + (lease_length % 4) * 3 / 4);
        options.push(DhcpOption::RebindingTimeValue(v));
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
        let client_id = ClientIdentifier::from(&req);
        let options = match self.records.get(&client_id) {
            Some(record) => {
                let mut options = Vec::with_capacity(record.options.len() + 1);
                options.push(DhcpOption::DhcpMessageType(MessageType::DHCPACK));
                options.extend(record.options.iter().cloned());
                options
            }
            None => return Err(ServerError::UnknownClientId(client_id)),
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
        reason: NakReason,
    ) -> Result<(Message, ResponseTarget), ServerError> {
        let options = vec![
            DhcpOption::DhcpMessageType(MessageType::DHCPNAK),
            DhcpOption::ServerIdentifier(self.get_server_ip(&req)?),
            DhcpOption::Message(format!("{}", reason)),
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
            Ok((nak, ResponseTarget::Broadcast))
        } else {
            nak.bdcast_flag = true;
            let giaddr = nak.giaddr;
            Ok((nak, ResponseTarget::Unicast(giaddr, None)))
        }
    }

    /// Determines the server identifier to use in DHCP responses. This
    /// identifier is also the address the server should use to communicate with
    /// the client.
    ///
    /// RFC 2131, Section 4.1, https://tools.ietf.org/html/rfc2131#section-4.1
    ///
    ///   The 'server identifier' field is used both to identify a DHCP server
    ///   in a DHCP message and as a destination address from clients to
    ///   servers.  A server with multiple network addresses MUST be prepared
    ///   to to accept any of its network addresses as identifying that server
    ///   in a DHCP message.  To accommodate potentially incomplete network
    ///   connectivity, a server MUST choose an address as a 'server
    ///   identifier' that, to the best of the server's knowledge, is reachable
    ///   from the client.  For example, if the DHCP server and the DHCP client
    ///   are connected to the same subnet (i.e., the 'giaddr' field in the
    ///   message from the client is zero), the server SHOULD select the IP
    ///   address the server is using for communication on that subnet as the
    ///   'server identifier'.
    fn get_server_ip(&self, req: &Message) -> Result<Ipv4Addr, ServerError> {
        match get_server_id_from(&req) {
            Some(addr) => {
                if self.params.server_ips.contains(&addr) {
                    Ok(addr)
                } else {
                    Err(ServerError::IncorrectDHCPServer(addr))
                }
            }
            // TODO(fxbug.dev/21423): This IP should be chosen based on the
            // subnet of the client.
            None => Ok(*self.params.server_ips.first().ok_or(ServerError::ServerMissingIpAddr)?),
        }
    }

    /// Releases all allocated IP addresses whose leases have expired back to
    /// the pool of addresses available for allocation.
    fn release_expired_leases(&mut self) -> Result<(), ServerError> {
        let Self { records, pool, time_source, store, .. } = self;
        let now = time_source
            .now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_err(|std::time::SystemTimeError { .. }| ServerError::ServerTimeError)?;
        records
            .iter_mut()
            .filter(|(_id, record)| record.current.is_some() && record.expired(now))
            .try_for_each(|(id, record)| {
                let () = match release_leased_addr(id, record, pool, store) {
                    Ok(()) => (),
                    // Panic because server's state is irrecoverably inconsistent.
                    Err(ServerError::ServerAddressPoolFailure(e)) => {
                        panic!("fatal inconsistency in server address pool: {}", e)
                    }
                    Err(ServerError::DataStoreUpdateFailure(e)) => {
                        log::warn!("failed to update data store: {}", e)
                    }
                    Err(e) => return Err(e),
                };
                Ok(())
            })
    }

    /// Saves current parameters to stash.
    fn save_params(&mut self) -> Result<(), Status> {
        if let Some(store) = self.store.as_mut() {
            store.store_parameters(&self.params).map_err(|e| {
                log::warn!("store_parameters({:?}) in stash failed: {}", self.params, e);
                fuchsia_zircon::Status::INTERNAL
            })
        } else {
            Ok(())
        }
    }
}

// TODO(https://fxbug.dev/74998): Find an alternative to panicking.
fn release_leased_addr<DS: DataStore>(
    id: &ClientIdentifier,
    record: &mut LeaseRecord,
    pool: &mut AddressPool,
    store: &mut Option<DS>,
) -> Result<(), ServerError> {
    if let Some(addr) = record.current.take() {
        record.previous = Some(addr);
        let () = pool.release_addr(addr)?;
        if let Some(store) = store {
            let () = store
                .insert(id, record)
                .map_err(|e| ServerError::DataStoreUpdateFailure(anyhow::Error::from(e).into()))?;
        }
    } else {
        panic!("attempted to release lease that has already been released: {:?}", record);
    }
    Ok(())
}

/// The ability to dispatch fuchsia.net.dhcp.Server protocol requests and return a value.
///
/// Implementers of this trait can be used as the backing server-side logic of the
/// fuchsia.net.dhcp.Server protocol. Implementers must maintain a store of DHCP Options, DHCP
/// server parameters, and leases issued to clients, and support the trait methods to retrieve and
/// modify these stores.
pub trait ServerDispatcher {
    /// Validates the current set of server parameters returning a reference to
    /// the parameters if the configuration is valid or an error otherwise.
    fn try_validate_parameters(&self) -> Result<&ServerParameters, Status>;

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
    /// Resets all DHCP options to have no value.
    fn dispatch_reset_options(&mut self) -> Result<(), Status>;
    /// Resets all DHCP server parameters to their default values in `defaults`.
    fn dispatch_reset_parameters(&mut self, defaults: &ServerParameters) -> Result<(), Status>;
    /// Clears all leases from the store maintained by the ServerDispatcher.
    fn dispatch_clear_leases(&mut self) -> Result<(), Status>;
}

impl<DS: DataStore, TS: SystemTimeSource> ServerDispatcher for Server<DS, TS> {
    fn try_validate_parameters(&self) -> Result<&ServerParameters, Status> {
        if !self.params.is_valid() {
            return Err(Status::INVALID_ARGS);
        }

        // TODO(fxbug.dev/62558): rethink this check and this function.
        if self.pool.universe.is_empty() {
            log::error!("Server validation failed: Address pool is empty");
            return Err(Status::INVALID_ARGS);
        }
        Ok(&self.params)
    }

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
        name: fidl_fuchsia_net_dhcp::ParameterName,
    ) -> Result<fidl_fuchsia_net_dhcp::Parameter, Status> {
        match name {
            fidl_fuchsia_net_dhcp::ParameterName::IpAddrs => {
                Ok(fidl_fuchsia_net_dhcp::Parameter::IpAddrs(
                    self.params.server_ips.clone().into_fidl(),
                ))
            }
            fidl_fuchsia_net_dhcp::ParameterName::AddressPool => {
                Ok(fidl_fuchsia_net_dhcp::Parameter::AddressPool(
                    self.params.managed_addrs.clone().into_fidl(),
                ))
            }
            fidl_fuchsia_net_dhcp::ParameterName::LeaseLength => {
                Ok(fidl_fuchsia_net_dhcp::Parameter::Lease(
                    self.params.lease_length.clone().into_fidl(),
                ))
            }
            fidl_fuchsia_net_dhcp::ParameterName::PermittedMacs => {
                Ok(fidl_fuchsia_net_dhcp::Parameter::PermittedMacs(
                    self.params.permitted_macs.clone().into_fidl(),
                ))
            }
            fidl_fuchsia_net_dhcp::ParameterName::StaticallyAssignedAddrs => {
                Ok(fidl_fuchsia_net_dhcp::Parameter::StaticallyAssignedAddrs(
                    self.params.static_assignments.clone().into_fidl(),
                ))
            }
            fidl_fuchsia_net_dhcp::ParameterName::ArpProbe => {
                Ok(fidl_fuchsia_net_dhcp::Parameter::ArpProbe(self.params.arp_probe))
            }
            fidl_fuchsia_net_dhcp::ParameterName::BoundDeviceNames => {
                Ok(fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(
                    self.params.bound_device_names.clone(),
                ))
            }
        }
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
        let opts: Vec<DhcpOption> = self.options_repo.values().cloned().collect();
        if let Some(store) = self.store.as_mut() {
            let () = store.store_options(&opts).map_err(|e| {
                log::warn!("store_options({:?}) in stash failed: {}", opts, e);
                fuchsia_zircon::Status::INTERNAL
            })?;
        }
        Ok(())
    }

    fn dispatch_set_parameter(
        &mut self,
        value: fidl_fuchsia_net_dhcp::Parameter,
    ) -> Result<(), Status> {
        let () = match value {
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(ip_addrs) => {
                self.params.server_ips = Vec::<Ipv4Addr>::from_fidl(ip_addrs)
            }
            fidl_fuchsia_net_dhcp::Parameter::AddressPool(managed_addrs) => {
                // Be overzealous and do not allow the managed addresses to
                // change if we currently have leases.
                if !self.records.is_empty() {
                    return Err(Status::BAD_STATE);
                }

                self.params.managed_addrs =
                    match crate::configuration::ManagedAddresses::try_from_fidl(managed_addrs) {
                        Ok(managed_addrs) => managed_addrs,
                        Err(e) => {
                            log::info!(
                                "dispatch_set_parameter() got invalid AddressPool argument: {:?}",
                                e
                            );
                            return Err(Status::INVALID_ARGS);
                        }
                    };
                // Update the pool with the new parameters.
                self.pool = AddressPool::new(self.params.managed_addrs.pool_range());
            }
            fidl_fuchsia_net_dhcp::Parameter::Lease(lease_length) => {
                self.params.lease_length =
                    match crate::configuration::LeaseLength::try_from_fidl(lease_length) {
                        Ok(lease_length) => lease_length,
                        Err(e) => {
                            log::info!(
                                "dispatch_set_parameter() got invalid LeaseLength argument: {}",
                                e
                            );
                            return Err(Status::INVALID_ARGS);
                        }
                    }
            }
            fidl_fuchsia_net_dhcp::Parameter::PermittedMacs(permitted_macs) => {
                self.params.permitted_macs =
                    crate::configuration::PermittedMacs::from_fidl(permitted_macs)
            }
            fidl_fuchsia_net_dhcp::Parameter::StaticallyAssignedAddrs(static_assignments) => {
                self.params.static_assignments =
                    match crate::configuration::StaticAssignments::try_from_fidl(static_assignments)
                    {
                        Ok(static_assignments) => static_assignments,
                        Err(e) => {
                            log::info!("dispatch_set_parameter() got invalid StaticallyAssignedAddrs argument: {}", e);
                            return Err(Status::INVALID_ARGS);
                        }
                    }
            }
            fidl_fuchsia_net_dhcp::Parameter::ArpProbe(arp_probe) => {
                self.params.arp_probe = arp_probe
            }
            fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(bound_device_names) => {
                self.params.bound_device_names = bound_device_names
            }
            fidl_fuchsia_net_dhcp::ParameterUnknown!() => return Err(Status::INVALID_ARGS),
        };
        let () = self.save_params()?;
        Ok(())
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
        // Without this redundant borrow, the compiler will interpret this statement as a moving destructure.
        let ServerParameters {
            server_ips,
            managed_addrs,
            lease_length,
            permitted_macs,
            static_assignments,
            arp_probe,
            bound_device_names,
        } = &self.params;
        Ok(vec![
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(server_ips.clone().into_fidl()),
            fidl_fuchsia_net_dhcp::Parameter::AddressPool(managed_addrs.clone().into_fidl()),
            fidl_fuchsia_net_dhcp::Parameter::Lease(lease_length.clone().into_fidl()),
            fidl_fuchsia_net_dhcp::Parameter::PermittedMacs(permitted_macs.clone().into_fidl()),
            fidl_fuchsia_net_dhcp::Parameter::StaticallyAssignedAddrs(
                static_assignments.clone().into_fidl(),
            ),
            fidl_fuchsia_net_dhcp::Parameter::ArpProbe(*arp_probe),
            fidl_fuchsia_net_dhcp::Parameter::BoundDeviceNames(bound_device_names.clone()),
        ])
    }

    fn dispatch_reset_options(&mut self) -> Result<(), Status> {
        let () = self.options_repo.clear();
        let opts: Vec<DhcpOption> = self.options_repo.values().cloned().collect();
        if let Some(store) = self.store.as_mut() {
            let () = store.store_options(&opts).map_err(|e| {
                log::warn!("store_options({:?}) in stash failed: {}", opts, e);
                fuchsia_zircon::Status::INTERNAL
            })?;
        }
        Ok(())
    }

    fn dispatch_reset_parameters(&mut self, defaults: &ServerParameters) -> Result<(), Status> {
        self.params = defaults.clone();
        let () = self.save_params()?;
        Ok(())
    }

    fn dispatch_clear_leases(&mut self) -> Result<(), Status> {
        let Self { records, pool, store, .. } = self;
        for (id, LeaseRecord { current, .. }) in records.drain() {
            if let Some(current) = current {
                let () = match pool.release_addr(current) {
                    Ok(()) => (),
                    // Panic on failure because server has irrecoverable inconsistent state.
                    Err(e) => panic!("fatal server release address failure: {}", e),
                };
            }
            if let Some(store) = store {
                let () = store.delete(&id).map_err(|e| {
                    log::warn!("delete({}) failed: {:?}", id, e);
                    fuchsia_zircon::Status::INTERNAL
                })?;
            }
        }
        Ok(())
    }
}

/// A mapping of clients to their lease records.
///
/// The server should store a record for each client to which it has sent
/// a DHCPOFFER message.
pub type ClientRecords = HashMap<ClientIdentifier, LeaseRecord>;

/// A record of a DHCP client's configuration settings.
///
/// A client's `ClientIdentifier` maps to the `LeaseRecord`: this mapping
/// is stored in the `Server`s `ClientRecords` instance at runtime, and in
/// `fuchsia.stash` persistent storage.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LeaseRecord {
    current: Option<Ipv4Addr>,
    previous: Option<Ipv4Addr>,
    options: Vec<DhcpOption>,
    lease_start_epoch_seconds: u64,
    lease_length_seconds: u32,
}

#[cfg(test)]
impl Default for LeaseRecord {
    fn default() -> Self {
        LeaseRecord {
            current: None,
            previous: None,
            options: Vec::new(),
            lease_start_epoch_seconds: std::u64::MIN,
            lease_length_seconds: std::u32::MAX,
        }
    }
}

impl PartialEq for LeaseRecord {
    fn eq(&self, other: &Self) -> bool {
        // Only compare directly comparable fields.
        let LeaseRecord {
            current,
            previous,
            options,
            lease_start_epoch_seconds: _not_comparable,
            lease_length_seconds,
        } = self;
        let LeaseRecord {
            current: other_current,
            previous: other_previous,
            options: other_options,
            lease_start_epoch_seconds: _other_not_comparable,
            lease_length_seconds: other_lease_length_seconds,
        } = other;
        current == other_current
            && previous == other_previous
            && options == other_options
            && lease_length_seconds == other_lease_length_seconds
    }
}

impl LeaseRecord {
    fn new(
        current: Option<Ipv4Addr>,
        options: Vec<DhcpOption>,
        lease_start: std::time::SystemTime,
        lease_length_seconds: u32,
    ) -> Result<Self, Error> {
        let lease_start_epoch_seconds =
            lease_start.duration_since(std::time::UNIX_EPOCH)?.as_secs();
        Ok(Self {
            current,
            previous: None,
            options,
            lease_start_epoch_seconds,
            lease_length_seconds,
        })
    }

    fn expired(&self, since_unix_epoch: std::time::Duration) -> bool {
        let LeaseRecord { lease_start_epoch_seconds, lease_length_seconds, .. } = self;
        let end = std::time::Duration::from_secs(
            *lease_start_epoch_seconds + u64::from(*lease_length_seconds),
        );
        since_unix_epoch >= end
    }
}

/// The pool of addresses managed by the server.
#[derive(Debug)]
struct AddressPool {
    // Morally immutable after construction, this is the full set of addresses
    // this pool manages, both allocated and available.
    //
    // TODO(https://fxbug.dev/74521): make this type std::ops::Range.
    universe: BTreeSet<Ipv4Addr>,
    allocated: BTreeSet<Ipv4Addr>,
}

//This is a wrapper around different errors that could be returned by
// the DHCP server address pool during address allocation/de-allocation.
#[derive(Debug, Error, PartialEq)]
pub enum AddressPoolError {
    #[error("address pool does not have any available ip to hand out")]
    Ipv4AddrExhaustion,

    #[error("attempted to allocate already allocated ip: {}", _0)]
    AllocatedIpv4AddrAllocation(Ipv4Addr),

    #[error("attempted to release unallocated ip: {}", _0)]
    UnallocatedIpv4AddrRelease(Ipv4Addr),

    #[error("attempted to interact with out-of-pool ip: {}", _0)]
    UnmanagedIpv4Addr(Ipv4Addr),
}

impl AddressPool {
    fn new<T: Iterator<Item = Ipv4Addr>>(addresses: T) -> Self {
        Self { universe: addresses.collect(), allocated: BTreeSet::new() }
    }

    fn available(&self) -> impl Iterator<Item = Ipv4Addr> + '_ {
        let Self { universe: range, allocated } = self;
        range.difference(allocated).copied()
    }

    fn allocate_addr(&mut self, addr: Ipv4Addr) -> Result<(), AddressPoolError> {
        if !self.universe.contains(&addr) {
            Err(AddressPoolError::UnmanagedIpv4Addr(addr))
        } else {
            if !self.allocated.insert(addr) {
                Err(AddressPoolError::AllocatedIpv4AddrAllocation(addr))
            } else {
                Ok(())
            }
        }
    }

    fn release_addr(&mut self, addr: Ipv4Addr) -> Result<(), AddressPoolError> {
        if !self.universe.contains(&addr) {
            Err(AddressPoolError::UnmanagedIpv4Addr(addr))
        } else {
            if !self.allocated.remove(&addr) {
                Err(AddressPoolError::UnallocatedIpv4AddrRelease(addr))
            } else {
                Ok(())
            }
        }
    }

    fn addr_is_available(&self, addr: Ipv4Addr) -> bool {
        self.universe.contains(&addr) && !self.allocated.contains(&addr)
    }

    fn addr_is_allocated(&self, addr: Ipv4Addr) -> bool {
        self.allocated.contains(&addr)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ClientState {
    Selecting,
    InitReboot,
    Renewing,
}

// Cf. RFC 2131 Table 5: https://tools.ietf.org/html/rfc2131#page-37
fn validate_discover(disc: &Message) -> Result<(), ServerError> {
    use std::string::ToString as _;
    if disc.op != OpCode::BOOTREQUEST {
        return Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
            field: String::from("op"),
            value: OpCode::BOOTREPLY.to_string(),
            msg_type: MessageType::DHCPDISCOVER,
        }));
    }
    if !disc.ciaddr.is_unspecified() {
        return Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
            field: String::from("ciaddr"),
            value: disc.ciaddr.to_string(),
            msg_type: MessageType::DHCPDISCOVER,
        }));
    }
    if !disc.yiaddr.is_unspecified() {
        return Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
            field: String::from("yiaddr"),
            value: disc.yiaddr.to_string(),
            msg_type: MessageType::DHCPDISCOVER,
        }));
    }
    if !disc.siaddr.is_unspecified() {
        return Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
            field: String::from("siaddr"),
            value: disc.siaddr.to_string(),
            msg_type: MessageType::DHCPDISCOVER,
        }));
    }
    // Do not check giaddr, because although a client will never set it, an
    // intervening relay agent may have done.
    if let Some(DhcpOption::ServerIdentifier(addr)) = disc.options.iter().find(|opt| match opt {
        DhcpOption::ServerIdentifier(_) => true,
        _ => false,
    }) {
        return Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
            field: String::from("ServerIdentifier"),
            value: addr.to_string(),
            msg_type: MessageType::DHCPDISCOVER,
        }));
    }
    Ok(())
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
        config.managed_addrs.mask.apply_to(&client_ip)
            == config.managed_addrs.mask.apply_to(server_ip)
    })
}

fn get_client_state(msg: &Message) -> Result<ClientState, ()> {
    let server_id = get_server_id_from(&msg);
    let requested_ip = get_requested_ip_addr(&msg);

    // State classification from: https://tools.ietf.org/html/rfc2131#section-4.3.2
    //
    // DHCPREQUEST generated during SELECTING state:
    //
    // Client inserts the address of the selected server in 'server identifier', 'ciaddr' MUST be
    // zero, 'requested IP address' MUST be filled in with the yiaddr value from the chosen
    // DHCPOFFER.
    //
    // DHCPREQUEST generated during INIT-REBOOT state:
    //
    // 'server identifier' MUST NOT be filled in, 'requested IP address' option MUST be
    // filled in with client's notion of its previously assigned address. 'ciaddr' MUST be
    // zero.
    //
    // DHCPREQUEST generated during RENEWING state:
    //
    // 'server identifier' MUST NOT be filled in, 'requested IP address' option MUST NOT be filled
    // in, 'ciaddr' MUST be filled in with client's IP address.
    //
    // TODO(fxbug.dev/64978): Distinguish between clients in RENEWING and REBINDING states
    if server_id.is_some() && msg.ciaddr.is_unspecified() && requested_ip.is_some() {
        Ok(ClientState::Selecting)
    } else if server_id.is_none() && requested_ip.is_some() && msg.ciaddr.is_unspecified() {
        Ok(ClientState::InitReboot)
    } else if server_id.is_none() && requested_ip.is_none() && !msg.ciaddr.is_unspecified() {
        Ok(ClientState::Renewing)
    } else {
        Err(())
    }
}

fn get_requested_ip_addr(req: &Message) -> Option<Ipv4Addr> {
    req.options.iter().find_map(|opt| {
        if let DhcpOption::RequestedIpAddress(addr) = opt {
            Some(*addr)
        } else {
            None
        }
    })
}

enum NakReason {
    ClientValidationFailure(ServerError),
    DifferentSubnets,
}

impl std::fmt::Display for NakReason {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::ClientValidationFailure(e) => {
                write!(f, "requested ip is not assigned to client: {}", e)
            }
            Self::DifferentSubnets => {
                write!(f, "client and server are in different subnets")
            }
        }
    }
}

pub fn get_server_id_from(req: &Message) -> Option<Ipv4Addr> {
    req.options.iter().find_map(|opt| match opt {
        DhcpOption::ServerIdentifier(addr) => Some(*addr),
        _ => None,
    })
}

#[cfg(test)]
pub mod tests {

    use crate::configuration::{
        LeaseLength, ManagedAddresses, PermittedMacs, StaticAssignments, SubnetMask,
    };
    use crate::protocol::{
        DhcpOption, FidlCompatible as _, IntoFidlExt as _, Message, MessageType, OpCode,
        OptionCode, ProtocolError,
    };
    use crate::server::{
        get_client_state, validate_discover, AddressPool, AddressPoolError, ClientIdentifier,
        ClientState, DataStore, LeaseRecord, NakReason, ResponseTarget, ServerAction,
        ServerDispatcher, ServerError, ServerParameters, SystemTimeSource,
    };
    use anyhow::Error;
    use datastore::{ActionRecordingDataStore, DataStoreAction};
    use fuchsia_zircon::Status;
    use net_declare::{fidl_ip_v4, std_ip_v4};
    use net_types::ethernet::Mac as MacAddr;
    use rand::Rng;
    use std::cell::RefCell;
    use std::collections::{BTreeSet, HashMap, HashSet};
    use std::convert::TryFrom as _;
    use std::iter::FromIterator as _;
    use std::net::Ipv4Addr;
    use std::rc::Rc;
    use std::time::{Duration, SystemTime};

    mod datastore {
        use crate::protocol::{DhcpOption, OptionCode};
        use crate::server::{
            ClientIdentifier, ClientRecords, DataStore, LeaseRecord, ServerParameters,
        };
        use std::collections::HashMap;

        pub struct ActionRecordingDataStore {
            actions: Vec<DataStoreAction>,
        }

        #[derive(Clone, Debug, PartialEq)]
        pub enum DataStoreAction {
            StoreClientRecord { client_id: ClientIdentifier, record: LeaseRecord },
            StoreOptions { opts: Vec<DhcpOption> },
            StoreParameters { params: ServerParameters },
            LoadClientRecords,
            LoadOptions,
            Delete { client_id: ClientIdentifier },
        }

        #[derive(Debug, thiserror::Error)]
        #[error(transparent)]
        pub struct ActionRecordingError(#[from] anyhow::Error);

        impl ActionRecordingDataStore {
            pub fn new() -> Self {
                Self { actions: Vec::new() }
            }

            pub fn push_action(&mut self, cmd: DataStoreAction) -> () {
                let Self { actions } = self;
                actions.push(cmd)
            }

            pub fn actions(&mut self) -> std::vec::Drain<'_, DataStoreAction> {
                let Self { actions } = self;
                actions.drain(..)
            }

            pub fn load_client_records(&mut self) -> Result<ClientRecords, ActionRecordingError> {
                let () = self.push_action(DataStoreAction::LoadClientRecords);
                Ok(HashMap::new())
            }

            pub fn load_options(
                &mut self,
            ) -> Result<HashMap<OptionCode, DhcpOption>, ActionRecordingError> {
                let () = self.push_action(DataStoreAction::LoadOptions);
                Ok(HashMap::new())
            }
        }

        impl Drop for ActionRecordingDataStore {
            fn drop(&mut self) {
                let Self { actions } = self;
                assert!(actions.is_empty())
            }
        }

        impl DataStore for ActionRecordingDataStore {
            type Error = ActionRecordingError;

            fn insert(
                &mut self,
                client_id: &ClientIdentifier,
                record: &LeaseRecord,
            ) -> Result<(), Self::Error> {
                Ok(self.push_action(DataStoreAction::StoreClientRecord {
                    client_id: client_id.clone(),
                    record: record.clone(),
                }))
            }

            fn store_options(&mut self, opts: &[DhcpOption]) -> Result<(), Self::Error> {
                Ok(self.push_action(DataStoreAction::StoreOptions { opts: Vec::from(opts) }))
            }

            fn store_parameters(&mut self, params: &ServerParameters) -> Result<(), Self::Error> {
                Ok(self.push_action(DataStoreAction::StoreParameters { params: params.clone() }))
            }

            fn delete(&mut self, client_id: &ClientIdentifier) -> Result<(), Self::Error> {
                Ok(self.push_action(DataStoreAction::Delete { client_id: client_id.clone() }))
            }
        }
    }

    // UTC time can go backwards (https://fuchsia.dev/fuchsia-src/concepts/time/utc/behavior),
    // using `SystemTime::now` has the possibility to introduce flakiness to tests. This struct
    // makes sure we can get non-decreasing `SystemTime`s in a test environment.
    #[derive(Clone)]
    struct TestSystemTime(Rc<RefCell<SystemTime>>);

    impl SystemTimeSource for TestSystemTime {
        fn with_current_time() -> Self {
            Self(Rc::new(RefCell::new(SystemTime::now())))
        }
        fn now(&self) -> SystemTime {
            let TestSystemTime(current) = self;
            *current.borrow()
        }
    }

    impl TestSystemTime {
        pub(super) fn move_forward(&mut self, duration: Duration) {
            let TestSystemTime(current) = self;
            *current.borrow_mut() += duration;
        }
    }

    type Server<DS = ActionRecordingDataStore> = super::Server<DS, TestSystemTime>;

    fn default_server_params() -> Result<ServerParameters, Error> {
        test_server_params(
            Vec::new(),
            LeaseLength { default_seconds: 60 * 60 * 24, max_seconds: 60 * 60 * 24 * 7 },
        )
    }

    fn test_server_params(
        server_ips: Vec<Ipv4Addr>,
        lease_length: LeaseLength,
    ) -> Result<ServerParameters, Error> {
        Ok(ServerParameters {
            server_ips,
            lease_length,
            managed_addrs: ManagedAddresses {
                mask: SubnetMask::try_from(24)?,
                pool_range_start: net_declare::std::ip_v4!("192.168.0.0"),
                pool_range_stop: net_declare::std::ip_v4!("192.168.0.0"),
            },
            permitted_macs: PermittedMacs(Vec::new()),
            static_assignments: StaticAssignments(HashMap::new()),
            arp_probe: false,
            bound_device_names: Vec::new(),
        })
    }

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
        MacAddr::new([octet1, octet2, octet3, octet4, octet5, octet6])
    }

    fn extract_message(server_response: ServerAction) -> Message {
        if let ServerAction::SendResponse(message, _destination) = server_response {
            message
        } else {
            panic!("expected a message in server response, received {:?}", server_response)
        }
    }

    fn get_router<DS: DataStore>(server: &Server<DS>) -> Result<Vec<Ipv4Addr>, ProtocolError> {
        let code = OptionCode::Router;
        match server.options_repo.get(&code) {
            Some(DhcpOption::Router(router)) => Some(router.clone()),
            option => panic!("unexpected entry {} => {:?}", &code, option),
        }
        .ok_or(ProtocolError::MissingOption(code))
    }

    fn get_dns_server<DS: DataStore>(server: &Server<DS>) -> Result<Vec<Ipv4Addr>, ProtocolError> {
        let code = OptionCode::DomainNameServer;
        match server.options_repo.get(&code) {
            Some(DhcpOption::DomainNameServer(dns_server)) => Some(dns_server.clone()),
            option => panic!("unexpected entry {} => {:?}", &code, option),
        }
        .ok_or(ProtocolError::MissingOption(code))
    }

    fn new_test_minimal_server_with_time_source() -> (Server, TestSystemTime) {
        let time_source = TestSystemTime::with_current_time();
        let params = test_server_params(
            vec![random_ipv4_generator()],
            LeaseLength { default_seconds: 100, max_seconds: 60 * 60 * 24 * 7 },
        )
        .expect("failed to create test server parameters");
        (
            super::Server {
                records: HashMap::new(),
                pool: AddressPool::new(params.managed_addrs.pool_range()),
                params,
                store: Some(ActionRecordingDataStore::new()),
                options_repo: HashMap::from_iter(vec![
                    (OptionCode::Router, DhcpOption::Router(vec![random_ipv4_generator()])),
                    (
                        OptionCode::DomainNameServer,
                        DhcpOption::DomainNameServer(vec![
                            std_ip_v4!("1.2.3.4"),
                            std_ip_v4!("4.3.2.1"),
                        ]),
                    ),
                ]),
                time_source: time_source.clone(),
            },
            time_source.clone(),
        )
    }

    fn new_test_minimal_server() -> Server {
        let (server, _time_source) = new_test_minimal_server_with_time_source();
        server
    }

    fn new_client_message(message_type: MessageType) -> Message {
        new_client_message_with_options(message_type, std::iter::empty())
    }

    fn new_client_message_with_options(
        message_type: MessageType,
        options: impl Iterator<Item = DhcpOption>,
    ) -> Message {
        Message {
            op: OpCode::BOOTREQUEST,
            xid: rand::thread_rng().gen(),
            secs: 0,
            bdcast_flag: true,
            ciaddr: Ipv4Addr::UNSPECIFIED,
            yiaddr: Ipv4Addr::UNSPECIFIED,
            siaddr: Ipv4Addr::UNSPECIFIED,
            giaddr: Ipv4Addr::UNSPECIFIED,
            chaddr: random_mac_generator(),
            sname: String::new(),
            file: String::new(),
            options: std::iter::once(DhcpOption::DhcpMessageType(message_type))
                .chain(std::iter::once(DhcpOption::ParameterRequestList(vec![
                    OptionCode::SubnetMask,
                    OptionCode::Router,
                    OptionCode::DomainNameServer,
                ])))
                .chain(options)
                .collect(),
        }
    }

    fn new_test_discover() -> Message {
        new_test_discover_with_options(std::iter::empty())
    }

    fn new_test_discover_with_options(options: impl Iterator<Item = DhcpOption>) -> Message {
        new_client_message_with_options(MessageType::DHCPDISCOVER, options)
    }

    fn new_server_message<DS: DataStore>(
        message_type: MessageType,
        client_message: &Message,
        server: &Server<DS>,
    ) -> Message {
        let Message {
            op: _,
            xid,
            secs: _,
            bdcast_flag,
            ciaddr: _,
            yiaddr: _,
            siaddr: _,
            giaddr: _,
            chaddr,
            sname: _,
            file: _,
            options: _,
        } = client_message;
        Message {
            op: OpCode::BOOTREPLY,
            xid: *xid,
            secs: 0,
            bdcast_flag: *bdcast_flag,
            ciaddr: Ipv4Addr::UNSPECIFIED,
            yiaddr: Ipv4Addr::UNSPECIFIED,
            siaddr: Ipv4Addr::UNSPECIFIED,
            giaddr: Ipv4Addr::UNSPECIFIED,
            chaddr: *chaddr,
            sname: String::new(),
            file: String::new(),
            options: vec![
                DhcpOption::DhcpMessageType(message_type),
                DhcpOption::ServerIdentifier(
                    server.get_server_ip(client_message).unwrap_or(Ipv4Addr::UNSPECIFIED),
                ),
            ],
        }
    }

    fn new_server_message_with_lease<DS: DataStore>(
        message_type: MessageType,
        client_message: &Message,
        server: &Server<DS>,
    ) -> Message {
        let mut msg = new_server_message(message_type, client_message, server);
        msg.options.extend(IntoIterator::into_iter([
            DhcpOption::IpAddressLeaseTime(100),
            DhcpOption::RenewalTimeValue(50),
            DhcpOption::RebindingTimeValue(75),
        ]));
        let () = add_server_options(&mut msg, server);
        msg
    }

    fn add_server_options<DS: DataStore>(msg: &mut Message, server: &Server<DS>) {
        msg.options.push(DhcpOption::SubnetMask(std_ip_v4!("255.255.255.0")));
        if let Some(routers) = match server.options_repo.get(&OptionCode::Router) {
            Some(DhcpOption::Router(v)) => Some(v),
            _ => None,
        } {
            msg.options.push(DhcpOption::Router(routers.clone()));
        }
        if let Some(servers) = match server.options_repo.get(&OptionCode::DomainNameServer) {
            Some(DhcpOption::DomainNameServer(v)) => Some(v),
            _ => None,
        } {
            msg.options.push(DhcpOption::DomainNameServer(servers.clone()));
        }
    }

    fn new_test_offer<DS: DataStore>(disc: &Message, server: &Server<DS>) -> Message {
        new_server_message_with_lease(MessageType::DHCPOFFER, disc, server)
    }

    fn new_test_request() -> Message {
        new_client_message(MessageType::DHCPREQUEST)
    }

    fn new_test_request_selecting_state<DS: DataStore>(
        server: &Server<DS>,
        requested_ip: Ipv4Addr,
    ) -> Message {
        let mut req = new_test_request();
        req.options.push(DhcpOption::RequestedIpAddress(requested_ip));
        req.options.push(DhcpOption::ServerIdentifier(
            server.get_server_ip(&req).unwrap_or(Ipv4Addr::UNSPECIFIED),
        ));
        req
    }

    fn new_test_ack<DS: DataStore>(req: &Message, server: &Server<DS>) -> Message {
        new_server_message_with_lease(MessageType::DHCPACK, req, server)
    }

    fn new_test_nak<DS: DataStore>(
        req: &Message,
        server: &Server<DS>,
        reason: NakReason,
    ) -> Message {
        let mut nak = new_server_message(MessageType::DHCPNAK, req, server);
        nak.options.push(DhcpOption::Message(format!("{}", reason)));
        nak
    }

    fn new_test_release() -> Message {
        new_client_message(MessageType::DHCPRELEASE)
    }

    fn new_test_inform() -> Message {
        new_client_message(MessageType::DHCPINFORM)
    }

    fn new_test_inform_ack<DS: DataStore>(req: &Message, server: &Server<DS>) -> Message {
        let mut msg = new_server_message(MessageType::DHCPACK, req, server);
        let () = add_server_options(&mut msg, server);
        msg
    }

    fn new_test_decline<DS: DataStore>(server: &Server<DS>) -> Message {
        let mut decline = new_client_message(MessageType::DHCPDECLINE);
        decline.options.push(DhcpOption::ServerIdentifier(
            server.get_server_ip(&decline).unwrap_or(Ipv4Addr::UNSPECIFIED),
        ));
        decline
    }

    #[test]
    fn test_dispatch_with_discover_returns_correct_offer_and_dest_giaddr_when_giaddr_set() {
        let mut server = new_test_minimal_server();
        let mut disc = new_test_discover();
        disc.giaddr = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&disc);

        let offer_ip = random_ipv4_generator();

        assert!(server.pool.universe.insert(offer_ip));

        let mut expected_offer = new_test_offer(&disc, &server);
        expected_offer.yiaddr = offer_ip;
        expected_offer.giaddr = disc.giaddr;

        let expected_dest = disc.giaddr;

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(
                expected_offer,
                ResponseTarget::Unicast(expected_dest, None)
            ))
        );
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_returns_correct_offer_and_dest_broadcast_when_giaddr_unspecified(
    ) {
        let mut server = new_test_minimal_server();
        let disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let offer_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(offer_ip));
        let expected_offer = {
            let mut expected_offer = new_test_offer(&disc, &server);
            expected_offer.yiaddr = offer_ip;
            expected_offer
        };

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(expected_offer, ResponseTarget::Broadcast))
        );
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_returns_correct_offer_and_dest_yiaddr_when_giaddr_and_ciaddr_unspecified_and_broadcast_bit_unset(
    ) {
        let mut server = new_test_minimal_server();
        let disc = {
            let mut disc = new_test_discover();
            disc.bdcast_flag = false;
            disc
        };
        let chaddr = disc.chaddr;
        let client_id = ClientIdentifier::from(&disc);

        let offer_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(offer_ip));
        let expected_offer = {
            let mut expected_offer = new_test_offer(&disc, &server);
            expected_offer.yiaddr = offer_ip;
            expected_offer
        };

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(
                expected_offer,
                ResponseTarget::Unicast(offer_ip, Some(chaddr))
            ))
        );
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_returns_correct_offer_and_dest_giaddr_if_giaddr_broadcast_bit_is_set(
    ) {
        let mut server = new_test_minimal_server();
        let giaddr = random_ipv4_generator();
        let disc = {
            let mut disc = new_test_discover();
            disc.giaddr = giaddr;
            disc
        };
        let client_id = ClientIdentifier::from(&disc);

        let offer_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(offer_ip));

        let expected_offer = {
            let mut expected_offer = new_test_offer(&disc, &server);
            expected_offer.yiaddr = offer_ip;
            expected_offer.giaddr = giaddr;
            expected_offer
        };

        assert_eq!(
            server.dispatch(disc),
            Ok(ServerAction::SendResponse(expected_offer, ResponseTarget::Unicast(giaddr, None)))
        );
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_returns_error_if_ciaddr_set() {
        use std::string::ToString as _;
        let mut server = new_test_minimal_server();
        let ciaddr = random_ipv4_generator();
        let disc = {
            let mut disc = new_test_discover();
            disc.ciaddr = ciaddr;
            disc
        };

        assert!(server.pool.universe.insert(random_ipv4_generator()));

        assert_eq!(
            server.dispatch(disc),
            Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
                field: String::from("ciaddr"),
                value: ciaddr.to_string(),
                msg_type: MessageType::DHCPDISCOVER
            }))
        );
    }

    #[test]
    fn test_dispatch_with_discover_updates_server_state() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let disc = new_test_discover();

        let offer_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&disc);

        assert!(server.pool.universe.insert(offer_ip));

        let server_id = server.params.server_ips.first().unwrap();
        let router = get_router(&server).expect("failed to get router");
        let dns_server = get_dns_server(&server).expect("failed to get dns server");
        let expected_client_record = LeaseRecord::new(
            Some(offer_ip),
            vec![
                DhcpOption::ServerIdentifier(*server_id),
                DhcpOption::IpAddressLeaseTime(server.params.lease_length.default_seconds),
                DhcpOption::RenewalTimeValue(server.params.lease_length.default_seconds / 2),
                DhcpOption::RebindingTimeValue(
                    (server.params.lease_length.default_seconds * 3) / 4,
                ),
                DhcpOption::SubnetMask(std_ip_v4!("255.255.255.0")),
                DhcpOption::Router(router),
                DhcpOption::DomainNameServer(dns_server),
            ],
            time_source.now(),
            server.params.lease_length.default_seconds,
        )
        .expect("failed to create lease record");

        let _response = server.dispatch(disc);

        let available: Vec<_> = server.pool.available().collect();
        assert!(available.is_empty(), "{:?}", available);
        assert_eq!(server.pool.allocated.len(), 1);
        assert_eq!(server.records.len(), 1);
        assert_eq!(server.records.get(&client_id), Some(&expected_client_record));
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    fn dispatch_with_discover_updates_stash_helper(
        additional_options: impl Iterator<Item = DhcpOption>,
    ) {
        let mut server = new_test_minimal_server();
        let disc = new_test_discover_with_options(additional_options);

        let client_id = ClientIdentifier::from(&disc);

        assert!(server.pool.universe.insert(random_ipv4_generator()));

        let server_action = server.dispatch(disc);
        assert!(server_action.is_ok());

        let client_record = server
            .records
            .get(&client_id)
            .unwrap_or_else(|| panic!("server records missing entry for {}", client_id))
            .clone();
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreClientRecord { client_id: id, record },
            ] if *id == client_id && *record == client_record
        );
    }

    #[test]
    fn test_dispatch_with_discover_updates_stash() {
        dispatch_with_discover_updates_stash_helper(std::iter::empty())
    }

    #[test]
    fn test_dispatch_with_discover_with_client_id_updates_stash() {
        dispatch_with_discover_updates_stash_helper(std::iter::once(DhcpOption::ClientIdentifier(
            vec![1, 2, 3, 4, 5],
        )))
    }

    #[test]
    fn test_dispatch_with_discover_client_binding_returns_bound_addr() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let bound_client_ip = random_ipv4_generator();

        assert!(server.pool.allocated.insert(bound_client_ip));
        assert!(server.pool.universe.insert(bound_client_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&disc),
                LeaseRecord::new(
                    Some(bound_client_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MAX
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, bound_client_ip);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreClientRecord {client_id: id, record: LeaseRecord {current: Some(ip), previous: None, .. }},
            ] if *id == client_id && *ip == bound_client_ip
        );
    }

    #[test]
    #[should_panic(expected = "active lease is unallocated in address pool")]
    fn test_dispatch_with_discover_client_binding_panics_when_addr_previously_not_allocated() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let disc = new_test_discover();

        let bound_client_ip = random_ipv4_generator();

        assert!(server.pool.universe.insert(bound_client_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&disc),
                LeaseRecord::new(
                    Some(bound_client_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MAX
                )
                .unwrap(),
            ),
            None
        );

        let _ = server.dispatch(disc);
    }

    #[test]
    fn test_dispatch_with_discover_expired_client_binding_returns_available_old_addr() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let bound_client_ip = random_ipv4_generator();

        assert!(server.pool.universe.insert(bound_client_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&disc),
                // Manually initialize because new() assumes an unexpired lease.
                LeaseRecord {
                    current: None,
                    previous: Some(bound_client_ip),
                    options: Vec::new(),
                    lease_start_epoch_seconds: time_source
                        .now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .expect("invalid time value")
                        .as_secs(),
                    lease_length_seconds: std::u32::MIN
                },
            ),
            None
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, bound_client_ip);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_expired_client_binding_unavailable_addr_returns_next_free_addr()
    {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let bound_client_ip = random_ipv4_generator();
        let free_ip = random_ipv4_generator();

        assert!(server.pool.allocated.insert(bound_client_ip));
        assert!(server.pool.universe.insert(free_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&disc),
                // Manually initialize because new() assumes an unexpired lease.
                LeaseRecord {
                    current: None,
                    previous: Some(bound_client_ip),
                    options: Vec::new(),
                    lease_start_epoch_seconds: time_source
                        .now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .expect("invalid time value")
                        .as_secs(),
                    lease_length_seconds: std::u32::MIN
                },
            ),
            None
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_expired_client_binding_returns_available_requested_addr() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let bound_client_ip = random_ipv4_generator();
        let requested_ip = random_ipv4_generator();

        assert!(server.pool.allocated.insert(bound_client_ip));
        assert!(server.pool.universe.insert(requested_ip));

        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&disc),
                // Manually initialize because new() assumes an unexpired lease.
                LeaseRecord {
                    current: None,
                    previous: Some(bound_client_ip),
                    options: Vec::new(),
                    lease_start_epoch_seconds: time_source
                        .now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .expect("invalid time value")
                        .as_secs(),
                    lease_length_seconds: std::u32::MIN
                },
            ),
            None
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, requested_ip);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_expired_client_binding_returns_next_addr_for_unavailable_requested_addr(
    ) {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let bound_client_ip = random_ipv4_generator();
        let requested_ip = random_ipv4_generator();
        let free_ip = random_ipv4_generator();

        assert!(server.pool.allocated.insert(bound_client_ip));
        assert!(server.pool.allocated.insert(requested_ip));
        assert!(server.pool.universe.insert(free_ip));

        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&disc),
                // Manually initialize because new() assumes an unexpired lease.
                LeaseRecord {
                    current: None,
                    previous: Some(bound_client_ip),
                    options: Vec::new(),
                    lease_start_epoch_seconds: time_source
                        .now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .expect("invalid time value")
                        .as_secs(),
                    lease_length_seconds: std::u32::MIN
                },
            ),
            None
        );

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_available_requested_addr_returns_requested_addr() {
        let mut server = new_test_minimal_server();
        let mut disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let requested_ip = random_ipv4_generator();
        let free_ip_1 = random_ipv4_generator();
        let free_ip_2 = random_ipv4_generator();

        assert!(server.pool.universe.insert(free_ip_1));
        assert!(server.pool.universe.insert(requested_ip));
        assert!(server.pool.universe.insert(free_ip_2));

        // Update discover message to request for a specific ip
        // which is available in server pool.
        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, requested_ip);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_unavailable_requested_addr_returns_next_free_addr() {
        let mut server = new_test_minimal_server();
        let mut disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let requested_ip = random_ipv4_generator();
        let free_ip_1 = random_ipv4_generator();

        assert!(server.pool.allocated.insert(requested_ip));
        assert!(server.pool.universe.insert(free_ip_1));

        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        let response = server.dispatch(disc).unwrap();

        assert_eq!(extract_message(response).yiaddr, free_ip_1);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_discover_unavailable_requested_addr_no_available_addr_returns_error() {
        let mut server = new_test_minimal_server();
        let mut disc = new_test_discover();

        let requested_ip = random_ipv4_generator();

        assert!(server.pool.allocated.insert(requested_ip));

        disc.options.push(DhcpOption::RequestedIpAddress(requested_ip));

        assert_eq!(
            server.dispatch(disc),
            Err(ServerError::ServerAddressPoolFailure(AddressPoolError::Ipv4AddrExhaustion))
        );
    }

    #[test]
    fn test_dispatch_with_discover_no_requested_addr_no_available_addr_returns_error() {
        let mut server = new_test_minimal_server();
        let disc = new_test_discover();
        server.pool.universe.clear();

        assert_eq!(
            server.dispatch(disc),
            Err(ServerError::ServerAddressPoolFailure(AddressPoolError::Ipv4AddrExhaustion))
        );
    }

    fn test_dispatch_with_bogus_client_message_returns_error(message_type: MessageType) {
        let mut server = new_test_minimal_server();

        assert_eq!(
            server.dispatch(Message {
                op: OpCode::BOOTREQUEST,
                xid: 0,
                secs: 0,
                bdcast_flag: false,
                ciaddr: Ipv4Addr::UNSPECIFIED,
                yiaddr: Ipv4Addr::UNSPECIFIED,
                siaddr: Ipv4Addr::UNSPECIFIED,
                giaddr: Ipv4Addr::UNSPECIFIED,
                chaddr: MacAddr::new([0; 6]),
                sname: String::new(),
                file: String::new(),
                options: vec![DhcpOption::DhcpMessageType(message_type),],
            }),
            Err(ServerError::UnexpectedClientMessageType(message_type))
        );
    }

    #[test]
    fn test_dispatch_with_client_offer_message_returns_error() {
        test_dispatch_with_bogus_client_message_returns_error(MessageType::DHCPOFFER)
    }

    #[test]
    fn test_dispatch_with_client_ack_message_returns_error() {
        test_dispatch_with_bogus_client_message_returns_error(MessageType::DHCPACK)
    }

    #[test]
    fn test_dispatch_with_client_nak_message_returns_error() {
        test_dispatch_with_bogus_client_message_returns_error(MessageType::DHCPNAK)
    }

    #[test]
    fn test_dispatch_with_selecting_request_returns_correct_ack() {
        test_selecting(true)
    }

    #[test]
    fn test_dispatch_with_selecting_request_bdcast_unset_returns_unicast_ack() {
        test_selecting(false)
    }

    fn test_selecting(broadcast: bool) {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let requested_ip = random_ipv4_generator();
        let req = {
            let mut req = new_test_request_selecting_state(&server, requested_ip);
            req.bdcast_flag = broadcast;
            req
        };

        assert!(server.pool.allocated.insert(requested_ip));

        let server_id = server.params.server_ips.first().unwrap();
        let router = get_router(&server).expect("failed to get router from server");
        let dns_server = get_dns_server(&server).expect("failed to get dns server from the server");
        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(requested_ip),
                    vec![
                        DhcpOption::ServerIdentifier(*server_id),
                        DhcpOption::IpAddressLeaseTime(server.params.lease_length.default_seconds),
                        DhcpOption::RenewalTimeValue(
                            server.params.lease_length.default_seconds / 2
                        ),
                        DhcpOption::RebindingTimeValue(
                            (server.params.lease_length.default_seconds * 3) / 4,
                        ),
                        DhcpOption::SubnetMask(std_ip_v4!("255.255.255.0")),
                        DhcpOption::Router(router),
                        DhcpOption::DomainNameServer(dns_server),
                    ],
                    time_source.now(),
                    std::u32::MAX,
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.yiaddr = requested_ip;
        let expected_response = if broadcast {
            Ok(ServerAction::SendResponse(expected_ack, ResponseTarget::Broadcast))
        } else {
            Ok(ServerAction::SendResponse(
                expected_ack,
                ResponseTarget::Unicast(requested_ip, Some(req.chaddr)),
            ))
        };
        assert_eq!(server.dispatch(req), expected_response,);
    }

    #[test]
    fn test_dispatch_with_selecting_request_maintains_server_invariants() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let requested_ip = random_ipv4_generator();
        let req = new_test_request_selecting_state(&server, requested_ip);

        let client_id = ClientIdentifier::from(&req);

        assert!(server.pool.allocated.insert(requested_ip));
        assert_matches::assert_matches!(
            server.records.insert(
                client_id.clone(),
                LeaseRecord::new(Some(requested_ip), Vec::new(), time_source.now(), std::u32::MAX)
                    .expect("failed to create lease record"),
            ),
            None
        );
        let _response = server.dispatch(req).unwrap();
        assert!(server.records.contains_key(&client_id));
        assert!(server.pool.addr_is_allocated(requested_ip));
    }

    #[test]
    fn test_dispatch_with_selecting_request_wrong_server_ip_returns_error() {
        let mut server = new_test_minimal_server();
        let mut req = new_test_request_selecting_state(&server, random_ipv4_generator());

        // Update request to have a server ip different from actual server ip.
        assert_matches::assert_matches!(
            req.options.remove(req.options.len() - 1),
            DhcpOption::ServerIdentifier { .. }
        );
        req.options.push(DhcpOption::ServerIdentifier(random_ipv4_generator()));

        let server_ip = *server.params.server_ips.first().expect("server missing IP address");
        assert_eq!(server.dispatch(req), Err(ServerError::IncorrectDHCPServer(server_ip)));
    }

    #[test]
    fn test_dispatch_with_selecting_request_unknown_client_mac_returns_nak_maintains_server_invariants(
    ) {
        let mut server = new_test_minimal_server();
        let requested_ip = random_ipv4_generator();
        let req = new_test_request_selecting_state(&server, requested_ip);

        let client_id = ClientIdentifier::from(&req);

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::UnknownClientId(client_id.clone())),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
        assert!(!server.records.contains_key(&client_id));
        assert!(!server.pool.addr_is_allocated(requested_ip));
    }

    #[test]
    fn test_dispatch_with_selecting_request_mismatched_requested_addr_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let client_requested_ip = random_ipv4_generator();
        let req = new_test_request_selecting_state(&server, client_requested_ip);

        let server_offered_ip = random_ipv4_generator();

        assert!(server.pool.allocated.insert(server_offered_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(server_offered_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MAX,
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::RequestedIpOfferIpMismatch(
                client_requested_ip,
                server_offered_ip,
            )),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_selecting_request_expired_client_binding_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let requested_ip = random_ipv4_generator();
        let req = new_test_request_selecting_state(&server, requested_ip);

        assert!(server.pool.universe.insert(requested_ip));
        assert!(server.pool.allocated.insert(requested_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(Some(requested_ip), Vec::new(), time_source.now(), std::u32::MIN)
                    .expect("failed to create lease record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::ExpiredLeaseRecord),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_selecting_request_no_reserved_addr_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let requested_ip = random_ipv4_generator();
        let req = new_test_request_selecting_state(&server, requested_ip);

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(Some(requested_ip), Vec::new(), time_source.now(), std::u32::MAX)
                    .expect("failed to create lese record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::UnidentifiedRequestedIp(requested_ip)),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_init_boot_request_returns_correct_ack() {
        test_init_reboot(true)
    }

    #[test]
    fn test_dispatch_with_init_boot_bdcast_unset_request_returns_correct_ack() {
        test_init_reboot(false)
    }

    fn test_init_reboot(broadcast: bool) {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut req = new_test_request();
        req.bdcast_flag = broadcast;

        // For init-reboot, server and requested ip must be on the same subnet.
        // Hard-coding ip values here to achieve that.
        let init_reboot_client_ip = std_ip_v4!("192.168.1.60");
        server.params.server_ips = vec![std_ip_v4!("192.168.1.1")];

        assert!(server.pool.allocated.insert(init_reboot_client_ip));

        // Update request to have the test requested ip.
        req.options.push(DhcpOption::RequestedIpAddress(init_reboot_client_ip));

        let server_id = server.params.server_ips.first().unwrap();
        let router = get_router(&server).expect("failed to get router");
        let dns_server = get_dns_server(&server).expect("failed to get dns server");
        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(init_reboot_client_ip),
                    vec![
                        DhcpOption::ServerIdentifier(*server_id),
                        DhcpOption::IpAddressLeaseTime(server.params.lease_length.default_seconds),
                        DhcpOption::RenewalTimeValue(
                            server.params.lease_length.default_seconds / 2
                        ),
                        DhcpOption::RebindingTimeValue(
                            (server.params.lease_length.default_seconds * 3) / 4,
                        ),
                        DhcpOption::SubnetMask(std_ip_v4!("255.255.255.0")),
                        DhcpOption::Router(router),
                        DhcpOption::DomainNameServer(dns_server),
                    ],
                    time_source.now(),
                    std::u32::MAX,
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.yiaddr = init_reboot_client_ip;

        let expected_response = if broadcast {
            Ok(ServerAction::SendResponse(expected_ack, ResponseTarget::Broadcast))
        } else {
            Ok(ServerAction::SendResponse(
                expected_ack,
                ResponseTarget::Unicast(init_reboot_client_ip, Some(req.chaddr)),
            ))
        };
        assert_eq!(server.dispatch(req), expected_response,);
    }

    #[test]
    fn test_dispatch_with_init_boot_request_client_on_wrong_subnet_returns_nak() {
        let mut server = new_test_minimal_server();
        let mut req = new_test_request();

        // Update request to have requested ip not on same subnet as server.
        req.options.push(DhcpOption::RequestedIpAddress(random_ipv4_generator()));

        // The returned nak should be from this recipient server.
        let expected_nak = new_test_nak(&req, &server, NakReason::DifferentSubnets);
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_init_boot_request_with_giaddr_set_returns_nak_with_broadcast_bit_set() {
        let mut server = new_test_minimal_server();
        let mut req = new_test_request();
        req.giaddr = random_ipv4_generator();

        // Update request to have requested ip not on same subnet as server,
        // to ensure we get a nak.
        req.options.push(DhcpOption::RequestedIpAddress(random_ipv4_generator()));

        let response = server.dispatch(req).unwrap();

        assert!(extract_message(response).bdcast_flag);
    }

    #[test]
    fn test_dispatch_with_init_boot_request_unknown_client_mac_returns_error() {
        let mut server = new_test_minimal_server();
        let mut req = new_test_request();

        let client_id = ClientIdentifier::from(&req);

        // Update requested ip and server ip to be on the same subnet.
        req.options.push(DhcpOption::RequestedIpAddress(std_ip_v4!("192.165.30.45")));
        server.params.server_ips = vec![std_ip_v4!("192.165.30.1")];

        assert_eq!(server.dispatch(req), Err(ServerError::UnknownClientId(client_id)));
    }

    #[test]
    fn test_dispatch_with_init_boot_request_mismatched_requested_addr_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut req = new_test_request();

        // Update requested ip and server ip to be on the same subnet.
        let init_reboot_client_ip = std_ip_v4!("192.165.25.4");
        req.options.push(DhcpOption::RequestedIpAddress(init_reboot_client_ip));
        server.params.server_ips = vec![std_ip_v4!("192.165.25.1")];

        let server_cached_ip = std_ip_v4!("192.165.25.10");
        assert!(server.pool.allocated.insert(server_cached_ip));
        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(server_cached_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MAX,
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::RequestedIpOfferIpMismatch(
                init_reboot_client_ip,
                server_cached_ip,
            )),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_init_boot_request_expired_client_binding_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut req = new_test_request();

        let init_reboot_client_ip = std_ip_v4!("192.165.25.4");
        req.options.push(DhcpOption::RequestedIpAddress(init_reboot_client_ip));
        server.params.server_ips = vec![std_ip_v4!("192.165.25.1")];

        assert!(server.pool.universe.insert(init_reboot_client_ip));
        assert!(server.pool.allocated.insert(init_reboot_client_ip));
        // Expire client binding to make it invalid.
        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(init_reboot_client_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MIN,
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::ExpiredLeaseRecord),
        );

        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_init_boot_request_no_reserved_addr_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut req = new_test_request();

        let init_reboot_client_ip = std_ip_v4!("192.165.25.4");
        req.options.push(DhcpOption::RequestedIpAddress(init_reboot_client_ip));
        server.params.server_ips = vec![std_ip_v4!("192.165.25.1")];

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(init_reboot_client_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MAX,
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::UnidentifiedRequestedIp(
                init_reboot_client_ip,
            )),
        );

        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_renewing_request_returns_correct_ack() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();

        assert!(server.pool.allocated.insert(bound_client_ip));
        req.ciaddr = bound_client_ip;

        let server_id = server.params.server_ips.first().unwrap();
        let router = get_router(&server).expect("failed to get router");
        let dns_server = get_dns_server(&server).expect("failed to get dns server");
        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(bound_client_ip),
                    vec![
                        DhcpOption::ServerIdentifier(*server_id),
                        DhcpOption::IpAddressLeaseTime(server.params.lease_length.default_seconds),
                        DhcpOption::RenewalTimeValue(
                            server.params.lease_length.default_seconds / 2
                        ),
                        DhcpOption::RebindingTimeValue(
                            (server.params.lease_length.default_seconds * 3) / 4,
                        ),
                        DhcpOption::SubnetMask(std_ip_v4!("255.255.255.0")),
                        DhcpOption::Router(router),
                        DhcpOption::DomainNameServer(dns_server),
                    ],
                    time_source.now(),
                    std::u32::MAX,
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let mut expected_ack = new_test_ack(&req, &server);
        expected_ack.yiaddr = bound_client_ip;
        expected_ack.ciaddr = bound_client_ip;

        let expected_dest = req.ciaddr;

        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(
                expected_ack,
                ResponseTarget::Unicast(expected_dest, None)
            ))
        );
    }

    #[test]
    fn test_dispatch_with_renewing_request_unknown_client_mac_returns_nak() {
        let mut server = new_test_minimal_server();
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&req);

        req.ciaddr = bound_client_ip;

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::UnknownClientId(client_id)),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_renewing_request_mismatched_requested_addr_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut req = new_test_request();

        let client_renewal_ip = random_ipv4_generator();
        let bound_client_ip = random_ipv4_generator();

        assert!(server.pool.allocated.insert(bound_client_ip));
        req.ciaddr = client_renewal_ip;

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(bound_client_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MAX
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::RequestedIpOfferIpMismatch(
                client_renewal_ip,
                bound_client_ip,
            )),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_renewing_request_expired_client_binding_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();

        assert!(server.pool.universe.insert(bound_client_ip));
        assert!(server.pool.allocated.insert(bound_client_ip));
        req.ciaddr = bound_client_ip;

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(bound_client_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MIN
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::ExpiredLeaseRecord),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_renewing_request_no_reserved_addr_returns_nak() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut req = new_test_request();

        let bound_client_ip = random_ipv4_generator();
        req.ciaddr = bound_client_ip;

        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(&req),
                LeaseRecord::new(
                    Some(bound_client_ip),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MAX
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        let expected_nak = new_test_nak(
            &req,
            &server,
            NakReason::ClientValidationFailure(ServerError::UnidentifiedRequestedIp(
                bound_client_ip,
            )),
        );
        assert_eq!(
            server.dispatch(req),
            Ok(ServerAction::SendResponse(expected_nak, ResponseTarget::Broadcast))
        );
    }

    #[test]
    fn test_dispatch_with_unknown_client_state_returns_error() {
        let mut server = new_test_minimal_server();

        let req = new_test_request();

        assert_eq!(server.dispatch(req), Err(ServerError::UnknownClientStateDuringRequest));
    }

    #[test]
    fn test_get_client_state_with_selecting_returns_selecting() {
        let mut req = new_test_request();

        // Selecting state request must have server id and requested ip populated.
        req.options.push(DhcpOption::ServerIdentifier(random_ipv4_generator()));
        req.options.push(DhcpOption::RequestedIpAddress(random_ipv4_generator()));

        assert_eq!(get_client_state(&req), Ok(ClientState::Selecting));
    }

    #[test]
    fn test_get_client_state_with_initreboot_returns_initreboot() {
        let mut req = new_test_request();

        // Init reboot state request must have requested ip populated.
        req.options.push(DhcpOption::RequestedIpAddress(random_ipv4_generator()));

        assert_eq!(get_client_state(&req), Ok(ClientState::InitReboot));
    }

    #[test]
    fn test_get_client_state_with_renewing_returns_renewing() {
        let mut req = new_test_request();

        // Renewing state request must have ciaddr populated.
        req.ciaddr = random_ipv4_generator();

        assert_eq!(get_client_state(&req), Ok(ClientState::Renewing));
    }

    #[test]
    fn test_get_client_state_with_unknown_returns_unknown() {
        let msg = new_test_request();

        assert_eq!(get_client_state(&msg), Err(()));
    }

    #[test]
    fn test_dispatch_with_client_msg_missing_message_type_option_returns_error() {
        let mut server = new_test_minimal_server();
        let mut msg = new_test_request();
        msg.options.clear();

        assert_eq!(
            server.dispatch(msg),
            Err(ServerError::ClientMessageError(ProtocolError::MissingOption(
                OptionCode::DhcpMessageType
            )))
        );
    }

    #[test]
    fn test_release_expired_leases_with_none_expired_releases_none() {
        let (mut server, mut time_source) = new_test_minimal_server_with_time_source();
        server.pool.universe.clear();

        // Insert client 1 bindings.
        let client_1_ip = random_ipv4_generator();
        let client_1_id = ClientIdentifier::from(random_mac_generator());
        let client_opts = [DhcpOption::IpAddressLeaseTime(u32::MAX)];
        assert!(server.pool.universe.insert(client_1_ip));
        server
            .store_client_record(client_1_ip, client_1_id.clone(), &client_opts)
            .expect("failed to store client record");

        // Insert client 2 bindings.
        let client_2_ip = random_ipv4_generator();
        let client_2_id = ClientIdentifier::from(random_mac_generator());
        assert!(server.pool.universe.insert(client_2_ip));
        server
            .store_client_record(client_2_ip, client_2_id.clone(), &client_opts)
            .expect("failed to store client record");

        // Insert client 3 bindings.
        let client_3_ip = random_ipv4_generator();
        let client_3_id = ClientIdentifier::from(random_mac_generator());
        assert!(server.pool.universe.insert(client_3_ip));
        server
            .store_client_record(client_3_ip, client_3_id.clone(), &client_opts)
            .expect("failed to store client record");

        let () = time_source.move_forward(Duration::from_secs(1));
        let () = server.release_expired_leases().expect("failed to release expired leases");

        let client_ips: BTreeSet<_> = [client_1_ip, client_2_ip, client_3_ip].into();
        assert_matches::assert_matches!(server.records.get(&client_1_id), Some(LeaseRecord {current: Some(ip), previous: None, ..}) if *ip == client_1_ip);
        assert_matches::assert_matches!(server.records.get(&client_2_id), Some(LeaseRecord {current: Some(ip), previous: None, ..}) if *ip == client_2_ip);
        assert_matches::assert_matches!(server.records.get(&client_3_id), Some(LeaseRecord {current: Some(ip), previous: None, ..}) if *ip == client_3_ip);
        let available: Vec<_> = server.pool.available().collect();
        assert!(available.is_empty(), "{:?}", available);
        assert_eq!(server.pool.allocated, client_ips);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreClientRecord { client_id: id_1, record: LeaseRecord { current: Some(ip1), previous: None, ..}, .. },
                DataStoreAction::StoreClientRecord { client_id: id_2, record: LeaseRecord { current: Some(ip2), previous: None, ..},.. },
                DataStoreAction::StoreClientRecord { client_id: id_3, record: LeaseRecord { current: Some(ip3), previous: None, ..},.. },
            ] if *id_1 == client_1_id && *id_2 == client_2_id && *id_3 == client_3_id &&
                *ip1 == client_1_ip && *ip2 == client_2_ip && *ip3 == client_3_ip
        );
    }

    #[test]
    fn test_release_expired_leases_with_all_expired_releases_all() {
        let (mut server, mut time_source) = new_test_minimal_server_with_time_source();
        server.pool.universe.clear();

        let client_1_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(client_1_ip));
        let client_1_id = ClientIdentifier::from(random_mac_generator());
        let () = server
            .store_client_record(
                client_1_ip,
                client_1_id.clone(),
                &[DhcpOption::IpAddressLeaseTime(0)],
            )
            .expect("failed to store client record");

        let client_2_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(client_2_ip));
        let client_2_id = ClientIdentifier::from(random_mac_generator());
        let () = server
            .store_client_record(
                client_2_ip,
                client_2_id.clone(),
                &[DhcpOption::IpAddressLeaseTime(0)],
            )
            .expect("failed to store client record");

        let client_3_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(client_3_ip));
        let client_3_id = ClientIdentifier::from(random_mac_generator());
        let () = server
            .store_client_record(
                client_3_ip,
                client_3_id.clone(),
                &[DhcpOption::IpAddressLeaseTime(0)],
            )
            .expect("failed to store client record");

        let () = time_source.move_forward(Duration::from_secs(1));
        let () = server.release_expired_leases().expect("failed to release expired leases");

        assert_eq!(server.records.len(), 3);
        assert_matches::assert_matches!(server.records.get(&client_1_id), Some(LeaseRecord {current: None, previous: Some(ip), ..}) if *ip == client_1_ip);
        assert_matches::assert_matches!(server.records.get(&client_2_id), Some(LeaseRecord {current: None, previous: Some(ip), ..}) if *ip == client_2_ip);
        assert_matches::assert_matches!(server.records.get(&client_3_id), Some(LeaseRecord {current: None, previous: Some(ip), ..}) if *ip == client_3_ip);
        assert_eq!(
            server.pool.available().collect::<HashSet<_>>(),
            [client_1_ip, client_2_ip, client_3_ip].into(),
        );
        assert!(server.pool.allocated.is_empty(), "{:?}", server.pool.allocated);
        // Delete actions occur in non-deterministic (HashMap iteration) order, so we must not
        // assert on the ordering of the deleted ids.
        assert_matches::assert_matches!(
            &server.store.expect("missing store").actions().as_slice()[..],
            [
                DataStoreAction::StoreClientRecord { client_id: id_1, record: LeaseRecord { current: Some(ip1), previous: None, ..}, .. },
                DataStoreAction::StoreClientRecord { client_id: id_2, record: LeaseRecord { current: Some(ip2), previous: None, ..},.. },
                DataStoreAction::StoreClientRecord { client_id: id_3, record: LeaseRecord { current: Some(ip3), previous: None, ..},.. },
                DataStoreAction::StoreClientRecord { client_id: update_id_1, record: LeaseRecord { current: None, previous: Some(update_ip_1), ..}, .. },
                DataStoreAction::StoreClientRecord { client_id: update_id_2, record: LeaseRecord { current: None, previous: Some(update_ip_2), ..}, .. },
                DataStoreAction::StoreClientRecord { client_id: update_id_3, record: LeaseRecord { current: None, previous: Some(update_ip_3), ..}, .. },
            ] if *id_1 == client_1_id && *id_2 == client_2_id && *id_3 == client_3_id &&
                *ip1 == client_1_ip && *ip2 == client_2_ip && *ip3 == client_3_ip &&
                [update_id_1, update_id_2, update_id_3].iter().all(|id| {
                    [&client_1_id, &client_2_id, &client_3_id].contains(id)
                }) &&
                [update_ip_1, update_ip_2, update_ip_3].iter().all(|ip| {
                    [&client_1_ip, &client_2_ip, &client_3_ip].contains(ip)
                })
        );
    }

    #[test]
    fn test_release_expired_leases_with_some_expired_releases_expired() {
        let (mut server, mut time_source) = new_test_minimal_server_with_time_source();
        server.pool.universe.clear();

        let client_1_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(client_1_ip));
        let client_1_id = ClientIdentifier::from(random_mac_generator());
        let () = server
            .store_client_record(
                client_1_ip,
                client_1_id.clone(),
                &[DhcpOption::IpAddressLeaseTime(u32::MAX)],
            )
            .expect("failed to store client record");

        let client_2_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(client_2_ip));
        let client_2_id = ClientIdentifier::from(random_mac_generator());
        let () = server
            .store_client_record(
                client_2_ip,
                client_2_id.clone(),
                &[DhcpOption::IpAddressLeaseTime(0)],
            )
            .expect("failed to store client record");

        let client_3_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(client_3_ip));
        let client_3_id = ClientIdentifier::from(random_mac_generator());
        let () = server
            .store_client_record(
                client_3_ip,
                client_3_id.clone(),
                &[DhcpOption::IpAddressLeaseTime(u32::MAX)],
            )
            .expect("failed to store client record");

        let () = time_source.move_forward(Duration::from_secs(1));
        let () = server.release_expired_leases().expect("failed to release expired leases");

        let client_ips: BTreeSet<_> = [client_1_ip, client_3_ip].into();
        assert_matches::assert_matches!(server.records.get(&client_1_id), Some(LeaseRecord {current: Some(ip), previous: None, ..}) if *ip == client_1_ip);
        assert_matches::assert_matches!(server.records.get(&client_2_id), Some(LeaseRecord {current: None, previous: Some(ip), ..}) if *ip == client_2_ip);
        assert_matches::assert_matches!(server.records.get(&client_3_id), Some(LeaseRecord {current: Some(ip), previous: None, ..}) if *ip == client_3_ip);
        assert_eq!(server.pool.available().collect::<Vec<_>>(), vec![client_2_ip]);
        assert_eq!(server.pool.allocated, client_ips);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreClientRecord { client_id: id_1, record: LeaseRecord { current: Some(ip1), previous: None, ..}, .. },
                DataStoreAction::StoreClientRecord { client_id: id_2, record: LeaseRecord { current: Some(ip2), previous: None, ..},.. },
                DataStoreAction::StoreClientRecord { client_id: id_3, record: LeaseRecord { current: Some(ip3), previous: None, ..},.. },
                DataStoreAction::StoreClientRecord { client_id: update_id_1, record: LeaseRecord { current: None, previous: Some(update_ip_1), ..}, ..},
            ] if *id_1 == client_1_id && *id_2 == client_2_id && *id_3 == client_3_id && *update_id_1 == client_2_id &&
                *ip1 == client_1_ip && *ip2 == client_2_ip && *ip3 == client_3_ip && *update_ip_1 == client_2_ip
        );
    }

    #[test]
    fn test_dispatch_with_known_release_updates_address_pool_retains_client_record() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut release = new_test_release();

        let release_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&release);

        assert!(server.pool.universe.insert(release_ip));
        assert!(server.pool.allocated.insert(release_ip));
        release.ciaddr = release_ip;

        let dns = random_ipv4_generator();
        let opts = vec![DhcpOption::DomainNameServer(vec![dns])];
        let test_client_record = |client_addr: Option<Ipv4Addr>, opts: Vec<DhcpOption>| {
            LeaseRecord::new(client_addr, opts, time_source.now(), u32::MAX).unwrap()
        };

        assert_matches::assert_matches!(
            server
                .records
                .insert(client_id.clone(), test_client_record(Some(release_ip), opts.clone())),
            None
        );

        assert_eq!(server.dispatch(release), Ok(ServerAction::AddressRelease(release_ip)));
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreClientRecord { client_id: id, record: LeaseRecord {current: None, previous: Some(ip), options, ..}}
            ] if *id == client_id  &&  *ip == release_ip && *options == opts
        );
        assert!(!server.pool.addr_is_allocated(release_ip), "addr marked allocated");
        assert!(server.pool.addr_is_available(release_ip), "addr not marked available");
        assert!(server.records.contains_key(&client_id), "client record not retained");
        assert_matches::assert_matches!(
            server.records.get(&client_id),
            Some(LeaseRecord {current: None, previous: Some(ip), options, lease_length_seconds, ..})
               if *ip == release_ip && *options == opts && *lease_length_seconds == u32::MAX
        );
    }

    #[test]
    fn test_dispatch_with_unknown_release_maintains_server_state_returns_unknown_mac_error() {
        let mut server = new_test_minimal_server();
        let mut release = new_test_release();

        let release_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&release);

        assert!(server.pool.allocated.insert(release_ip));
        release.ciaddr = release_ip;

        assert_eq!(server.dispatch(release), Err(ServerError::UnknownClientId(client_id)));

        assert!(server.pool.addr_is_allocated(release_ip), "addr not marked allocated");
        assert!(!server.pool.addr_is_available(release_ip), "addr still marked available");
    }

    #[test]
    fn test_dispatch_with_inform_returns_correct_ack() {
        let mut server = new_test_minimal_server();
        let mut inform = new_test_inform();

        let inform_client_ip = random_ipv4_generator();

        inform.ciaddr = inform_client_ip;

        let mut expected_ack = new_test_inform_ack(&inform, &server);
        expected_ack.ciaddr = inform_client_ip;

        let expected_dest = inform.ciaddr;

        assert_eq!(
            server.dispatch(inform),
            Ok(ServerAction::SendResponse(
                expected_ack,
                ResponseTarget::Unicast(expected_dest, None)
            ))
        );
    }

    #[test]
    fn test_dispatch_with_decline_for_allocated_addr_returns_ok() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&decline);

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        assert!(server.pool.allocated.insert(declined_ip));
        assert!(server.pool.universe.insert(declined_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                client_id.clone(),
                LeaseRecord::new(Some(declined_ip), Vec::new(), time_source.now(), std::u32::MAX)
                    .expect("failed to create lease record"),
            ),
            None
        );

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));
        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.records.contains_key(&client_id), "client record incorrectly retained");
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::Delete { client_id: id }] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_decline_for_available_addr_returns_ok() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&decline);

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));
        assert_matches::assert_matches!(
            server.records.insert(
                client_id.clone(),
                LeaseRecord::new(Some(declined_ip), Vec::new(), time_source.now(), std::u32::MAX)
                    .expect("failed to create lease record"),
            ),
            None
        );
        assert!(server.pool.universe.insert(declined_ip));

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));
        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.records.contains_key(&client_id), "client record incorrectly retained");
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::Delete { client_id: id }] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_decline_for_mismatched_addr_returns_err() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&decline);

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        let client_ip_according_to_server = random_ipv4_generator();
        assert!(server.pool.allocated.insert(client_ip_according_to_server));
        assert!(server.pool.universe.insert(declined_ip));

        // Server contains client bindings which reflect a different address
        // than the one being declined.
        assert_matches::assert_matches!(
            server.records.insert(
                client_id.clone(),
                LeaseRecord::new(
                    Some(client_ip_according_to_server),
                    Vec::new(),
                    time_source.now(),
                    std::u32::MAX,
                )
                .expect("failed to create lease record"),
            ),
            None
        );

        assert_eq!(
            server.dispatch(decline),
            Err(ServerError::DeclineIpMismatch {
                declined: Some(declined_ip),
                client: Some(client_ip_according_to_server)
            })
        );
        assert!(server.pool.addr_is_available(declined_ip), "addr not marked available");
        assert!(!server.pool.addr_is_allocated(declined_ip), "addr marked allocated");
        assert!(server.records.contains_key(&client_id), "client record deleted from records");
    }

    #[test]
    fn test_dispatch_with_decline_for_expired_lease_returns_ok() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&decline);

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        assert!(server.pool.universe.insert(declined_ip));

        assert_matches::assert_matches!(
            server.records.insert(
                client_id.clone(),
                LeaseRecord::new(Some(declined_ip), Vec::new(), time_source.now(), std::u32::MIN)
                    .expect("failed to create lease record"),
            ),
            None
        );

        assert_eq!(server.dispatch(decline), Ok(ServerAction::AddressDecline(declined_ip)));
        assert!(!server.pool.addr_is_available(declined_ip), "addr still marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(!server.records.contains_key(&client_id), "client record incorrectly retained");
        assert_matches::assert_matches!(
            server.store.expect("failed to create ").actions().as_slice(),
            [DataStoreAction::Delete { client_id: id }] if *id == client_id
        );
    }

    #[test]
    fn test_dispatch_with_decline_for_unknown_client_returns_err() {
        let mut server = new_test_minimal_server();
        let mut decline = new_test_decline(&server);

        let declined_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&decline);

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        assert!(server.pool.universe.insert(declined_ip));

        assert_eq!(
            server.dispatch(decline),
            Err(ServerError::DeclineFromUnrecognizedClient(client_id))
        );
        assert!(server.pool.addr_is_available(declined_ip), "addr not marked available");
        assert!(!server.pool.addr_is_allocated(declined_ip), "addr marked allocated");
    }

    #[test]
    fn test_dispatch_with_decline_for_incorrect_server_returns_err() {
        let (mut server, time_source) = new_test_minimal_server_with_time_source();
        server.params.server_ips = vec![random_ipv4_generator()];

        let mut decline = new_client_message(MessageType::DHCPDECLINE);
        let server_id = random_ipv4_generator();
        decline.options.push(DhcpOption::ServerIdentifier(server_id));

        let declined_ip = random_ipv4_generator();
        let client_id = ClientIdentifier::from(&decline);

        decline.options.push(DhcpOption::RequestedIpAddress(declined_ip));

        assert!(server.pool.allocated.insert(declined_ip));
        assert_matches::assert_matches!(
            server.records.insert(
                client_id.clone(),
                LeaseRecord::new(Some(declined_ip), Vec::new(), time_source.now(), std::u32::MAX)
                    .expect("failed to create lease record"),
            ),
            None
        );

        assert_eq!(server.dispatch(decline), Err(ServerError::IncorrectDHCPServer(server_id)));
        assert!(!server.pool.addr_is_available(declined_ip), "addr marked available");
        assert!(server.pool.addr_is_allocated(declined_ip), "addr not marked allocated");
        assert!(server.records.contains_key(&client_id), "client record not retained");
    }

    #[test]
    fn test_dispatch_with_decline_without_requested_addr_returns_err() {
        let mut server = new_test_minimal_server();
        let decline = new_test_decline(&server);

        assert_eq!(server.dispatch(decline), Err(ServerError::NoRequestedAddrForDecline));
    }

    #[test]
    fn test_client_requested_lease_time() {
        let mut disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let client_requested_time: u32 = 20;

        disc.options.push(DhcpOption::IpAddressLeaseTime(client_requested_time));

        let mut server = new_test_minimal_server();
        assert!(server.pool.universe.insert(random_ipv4_generator()));

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
            server.records.get(&client_id).unwrap().lease_length_seconds,
            client_requested_time,
        );
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_client_requested_lease_time_greater_than_max() {
        let mut disc = new_test_discover();
        let client_id = ClientIdentifier::from(&disc);

        let client_requested_time: u32 = 20;
        let server_max_lease_time: u32 = 10;

        disc.options.push(DhcpOption::IpAddressLeaseTime(client_requested_time));

        let mut server = new_test_minimal_server();
        assert!(server.pool.universe.insert(std_ip_v4!("195.168.1.45")));
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
            server.records.get(&client_id).unwrap().lease_length_seconds,
            server_max_lease_time,
        );
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreClientRecord {client_id: id, ..}] if *id == client_id
        );
    }

    #[test]
    fn test_server_dispatcher_get_option_with_unset_option_returns_not_found() {
        let server = new_test_minimal_server();
        let result = server.dispatch_get_option(fidl_fuchsia_net_dhcp::OptionCode::SubnetMask);
        assert_eq!(result, Err(Status::NOT_FOUND));
    }

    #[test]
    fn test_server_dispatcher_get_option_with_set_option_returns_option() {
        let mut server = new_test_minimal_server();
        let option = || fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_ip_v4!("255.255.255.0"));
        assert_matches::assert_matches!(
            server.options_repo.insert(
                OptionCode::SubnetMask,
                DhcpOption::try_from_fidl(option())
                    .expect("failed to convert dhcp option from fidl")
            ),
            None
        );
        let result = server
            .dispatch_get_option(fidl_fuchsia_net_dhcp::OptionCode::SubnetMask)
            .expect("failed to get dhcp option");
        assert_eq!(result, option());
    }

    #[test]
    fn test_server_dispatcher_get_parameter_returns_parameter() {
        let mut server = new_test_minimal_server();
        let addr = random_ipv4_generator();
        server.params.server_ips = vec![addr];
        let expected = fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![addr.into_fidl()]);
        let result = server
            .dispatch_get_parameter(fidl_fuchsia_net_dhcp::ParameterName::IpAddrs)
            .expect("failed to get dhcp option");
        assert_eq!(result, expected);
    }

    #[test]
    fn test_server_dispatcher_set_option_returns_unit() {
        let mut server = new_test_minimal_server();
        let option = || fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_ip_v4!("255.255.255.0"));
        let () = server.dispatch_set_option(option()).expect("failed to set dhcp option");
        let stored_option: DhcpOption =
            DhcpOption::try_from_fidl(option()).expect("failed to convert dhcp option from fidl");
        let code = stored_option.code();
        let result = server.options_repo.get(&code);
        assert_eq!(result, Some(&stored_option));
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreOptions { opts },
            ] if opts.contains(&stored_option)
        );
    }

    #[test]
    fn test_server_dispatcher_set_option_saves_to_stash() {
        let mask = [255, 255, 255, 0];
        let fidl_mask = fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_fuchsia_net::Ipv4Address {
            addr: mask,
        });
        let params = default_server_params().expect("failed to get default serve parameters");
        let mut server: Server = super::Server {
            records: HashMap::new(),
            pool: AddressPool::new(params.managed_addrs.pool_range()),
            params,
            store: Some(ActionRecordingDataStore::new()),
            options_repo: HashMap::new(),
            time_source: TestSystemTime::with_current_time(),
        };
        let () = server.dispatch_set_option(fidl_mask).expect("failed to set dhcp option");
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreOptions { opts },
            ] if *opts == vec![DhcpOption::SubnetMask(Ipv4Addr::from(mask))]
        );
    }

    #[test]
    fn test_server_dispatcher_set_parameter_saves_to_stash() {
        let (default, max) = (42, 100);
        let fidl_lease =
            fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                default: Some(default),
                max: Some(max),
                ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
            });
        let mut server = new_test_minimal_server();
        let () = server.dispatch_set_parameter(fidl_lease).expect("failed to set parameter");
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().next(),
            Some(DataStoreAction::StoreParameters {
                params: ServerParameters {
                    lease_length: LeaseLength { default_seconds: 42, max_seconds: 100 },
                    ..
                },
            })
        );
    }

    #[test]
    fn test_server_dispatcher_set_parameter() {
        let mut server = new_test_minimal_server();
        let addr = random_ipv4_generator();
        let valid_parameter = || fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![addr.into_fidl()]);
        let empty_lease_length =
            fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                default: None,
                max: None,
                ..fidl_fuchsia_net_dhcp::LeaseLength::EMPTY
            });
        let bad_prefix_length =
            fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
                prefix_length: Some(33),
                range_start: Some(fidl_ip_v4!("192.168.0.2")),
                range_stop: Some(fidl_ip_v4!("192.168.0.254")),
                ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
            });
        let mac = random_mac_generator().bytes();
        let duplicated_static_assignment =
            fidl_fuchsia_net_dhcp::Parameter::StaticallyAssignedAddrs(vec![
                fidl_fuchsia_net_dhcp::StaticAssignment {
                    host: Some(fidl_fuchsia_net::MacAddress { octets: mac.clone() }),
                    assigned_addr: Some(random_ipv4_generator().into_fidl()),
                    ..fidl_fuchsia_net_dhcp::StaticAssignment::EMPTY
                },
                fidl_fuchsia_net_dhcp::StaticAssignment {
                    host: Some(fidl_fuchsia_net::MacAddress { octets: mac.clone() }),
                    assigned_addr: Some(random_ipv4_generator().into_fidl()),
                    ..fidl_fuchsia_net_dhcp::StaticAssignment::EMPTY
                },
            ]);

        let () =
            server.dispatch_set_parameter(valid_parameter()).expect("failed to set dhcp parameter");
        assert_eq!(
            server
                .dispatch_get_parameter(fidl_fuchsia_net_dhcp::ParameterName::IpAddrs)
                .expect("failed to get dhcp parameter"),
            valid_parameter()
        );
        assert_eq!(
            server.dispatch_set_parameter(empty_lease_length),
            Err(fuchsia_zircon::Status::INVALID_ARGS)
        );
        assert_eq!(
            server.dispatch_set_parameter(bad_prefix_length),
            Err(fuchsia_zircon::Status::INVALID_ARGS)
        );
        assert_eq!(
            server.dispatch_set_parameter(duplicated_static_assignment),
            Err(fuchsia_zircon::Status::INVALID_ARGS)
        );
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreParameters { params }] if *params == server.params
        );
    }

    #[test]
    fn test_server_dispatcher_list_options_returns_set_options() {
        let mut server = new_test_minimal_server();
        let mask = || fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_ip_v4!("255.255.255.0"));
        let hostname = || fidl_fuchsia_net_dhcp::Option_::HostName(String::from("testhostname"));
        assert_matches::assert_matches!(
            server.options_repo.insert(
                OptionCode::SubnetMask,
                DhcpOption::try_from_fidl(mask()).expect("failed to convert dhcp option from fidl")
            ),
            None
        );
        assert_matches::assert_matches!(
            server.options_repo.insert(
                OptionCode::HostName,
                DhcpOption::try_from_fidl(hostname())
                    .expect("failed to convert dhcp option from fidl")
            ),
            None
        );
        let result = server.dispatch_list_options().expect("failed to list dhcp options");
        assert_eq!(result.len(), server.options_repo.len());
        assert!(result.contains(&mask()));
        assert!(result.contains(&hostname()));
    }

    #[test]
    fn test_server_dispatcher_list_parameters_returns_parameters() {
        let mut server = new_test_minimal_server();
        let addr = random_ipv4_generator();
        server.params.server_ips = vec![addr];
        let expected = fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![addr.into_fidl()]);
        let result = server.dispatch_list_parameters().expect("failed to list dhcp options");
        let params_fields_ct = 7;
        assert_eq!(result.len(), params_fields_ct);
        assert!(result.contains(&expected));
    }

    #[test]
    fn test_server_dispatcher_reset_options() {
        let mut server = new_test_minimal_server();
        let empty_map = HashMap::new();
        assert_ne!(empty_map, server.options_repo);
        let () = server.dispatch_reset_options().expect("failed to reset options");
        assert_eq!(empty_map, server.options_repo);
        let stored_opts = server
            .store
            .as_mut()
            .expect("missing store")
            .load_options()
            .expect("failed to load options");
        assert_eq!(empty_map, stored_opts);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreOptions { opts },
                DataStoreAction::LoadOptions
            ] if opts.is_empty()
        );
    }

    #[test]
    fn test_server_dispatcher_reset_parameters() {
        let mut server = new_test_minimal_server();
        let default_params = test_server_params(
            vec![std_ip_v4!("192.168.0.1")],
            LeaseLength { default_seconds: 86400, max_seconds: 86400 },
        )
        .expect("failed to get test server parameters");
        assert_ne!(default_params, server.params);
        let () =
            server.dispatch_reset_parameters(&default_params).expect("failed to reset parameters");
        assert_eq!(default_params, server.params);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreParameters { params }] if *params == default_params
        );
    }

    #[test]
    fn test_server_dispatcher_clear_leases() {
        let mut server = new_test_minimal_server();
        server.params.managed_addrs.pool_range_stop = std_ip_v4!("192.168.0.4");
        server.pool = AddressPool::new(server.params.managed_addrs.pool_range());
        let client = std_ip_v4!("192.168.0.2");
        let () = server
            .pool
            .allocate_addr(client)
            .unwrap_or_else(|err| panic!("allocate_addr({}) failed: {:?}", client, err));
        let client_id = ClientIdentifier::from(random_mac_generator());
        server.records = [(
            client_id.clone(),
            LeaseRecord {
                current: Some(client),
                previous: None,
                options: Vec::new(),
                lease_start_epoch_seconds: 0,
                lease_length_seconds: 42,
            },
        )]
        .into();
        let () = server.dispatch_clear_leases().expect("dispatch_clear_leases() failed");
        let empty_map = HashMap::new();
        assert_eq!(empty_map, server.records);
        assert!(server.pool.addr_is_available(client));
        assert!(!server.pool.addr_is_allocated(client));
        let stored_leases = server
            .store
            .as_mut()
            .expect("missing store")
            .load_client_records()
            .expect("load_client_records() failed");
        assert_eq!(empty_map, stored_leases);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::Delete { client_id: id },
                DataStoreAction::LoadClientRecords
            ] if *id == client_id
        );
    }

    #[test]
    fn test_server_dispatcher_validate_params() {
        let mut server = new_test_minimal_server();
        let () = server.pool.universe.clear();
        assert_eq!(server.try_validate_parameters(), Err(Status::INVALID_ARGS));
    }

    #[test]
    fn test_set_address_pool_fails_if_leases_present() {
        let mut server = new_test_minimal_server();
        assert_matches::assert_matches!(
            server.records.insert(
                ClientIdentifier::from(MacAddr::new([1, 2, 3, 4, 5, 6])),
                LeaseRecord::default(),
            ),
            None
        );
        assert_eq!(
            server.dispatch_set_parameter(fidl_fuchsia_net_dhcp::Parameter::AddressPool(
                fidl_fuchsia_net_dhcp::AddressPool {
                    prefix_length: Some(24),
                    range_start: Some(fidl_ip_v4!("192.168.0.2")),
                    range_stop: Some(fidl_ip_v4!("192.168.0.254")),
                    ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
                }
            )),
            Err(Status::BAD_STATE)
        );
    }

    #[test]
    fn test_set_address_pool_updates_internal_pool() {
        let mut server = new_test_minimal_server();
        let () = server.pool.universe.clear();
        let () = server
            .dispatch_set_parameter(fidl_fuchsia_net_dhcp::Parameter::AddressPool(
                fidl_fuchsia_net_dhcp::AddressPool {
                    prefix_length: Some(24),
                    range_start: Some(fidl_ip_v4!("192.168.0.2")),
                    range_stop: Some(fidl_ip_v4!("192.168.0.5")),
                    ..fidl_fuchsia_net_dhcp::AddressPool::EMPTY
                },
            ))
            .expect("failed to set parameter");
        assert_eq!(server.pool.available().count(), 3);
        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [DataStoreAction::StoreParameters { params }] if *params == server.params
        );
    }

    #[test]
    fn test_recovery_from_expired_persistent_record() {
        let client_ip = net_declare::std::ip_v4!("192.168.0.1");
        let mut time_source = TestSystemTime::with_current_time();
        const LEASE_EXPIRATION_SECONDS: u32 = 60;
        // The previous server has stored a stale client record.
        let mut store = ActionRecordingDataStore::new();
        let client_id = ClientIdentifier::from(random_mac_generator());
        let client_record = LeaseRecord::new(
            Some(client_ip),
            Vec::new(),
            time_source.now(),
            LEASE_EXPIRATION_SECONDS,
        )
        .expect("failed to create lease record");
        let () = store.insert(&client_id, &client_record).expect("failed to insert client record");
        // The record should become expired now.
        let () = time_source.move_forward(Duration::from_secs(LEASE_EXPIRATION_SECONDS.into()));

        // Only 192.168.0.1 is available.
        let params = ServerParameters {
            server_ips: Vec::new(),
            lease_length: LeaseLength {
                default_seconds: 60 * 60 * 24,
                max_seconds: 60 * 60 * 24 * 7,
            },
            managed_addrs: ManagedAddresses {
                mask: SubnetMask::try_from(24).unwrap(),
                pool_range_start: client_ip,
                pool_range_stop: net_declare::std::ip_v4!("192.168.0.2"),
            },
            permitted_macs: PermittedMacs(Vec::new()),
            static_assignments: StaticAssignments(HashMap::new()),
            arp_probe: false,
            bound_device_names: Vec::new(),
        };

        // The server should recover to a consistent state on the next start.
        let records: HashMap<_, _> =
            Some((client_id.clone(), client_record.clone())).into_iter().collect();
        let server: Server =
            Server::new_with_time_source(store, params, HashMap::new(), records, time_source)
                .expect("failed to create server");
        // Create a temporary because assert_matches! doesn't like turbo-fish type annotation.
        let contents: Vec<(&ClientIdentifier, &LeaseRecord)> = server.records.iter().collect();
        assert_matches::assert_matches!(
            contents.as_slice(),
            [(id, LeaseRecord {current: None, previous: Some(ip), ..})] if **id == client_id && *ip == client_ip
        );
        assert!(server.pool.allocated.is_empty());

        assert_eq!(server.pool.available().collect::<Vec<_>>(), vec![client_ip]);

        assert_matches::assert_matches!(
            server.store.expect("missing store").actions().as_slice(),
            [
                DataStoreAction::StoreClientRecord{ client_id: id1, record },
                DataStoreAction::StoreClientRecord{ client_id: id2, record: LeaseRecord {current: None, previous: Some(ip), ..} },
            ] if id1 == id2 && id2 == &client_id && record == &client_record && *ip == client_ip
        );
    }

    #[test]
    fn test_validate_discover() {
        use std::string::ToString as _;
        let mut disc = new_test_discover();
        disc.op = OpCode::BOOTREPLY;
        assert_eq!(
            validate_discover(&disc),
            Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
                field: String::from("op"),
                value: String::from("BOOTREPLY"),
                msg_type: MessageType::DHCPDISCOVER
            }))
        );
        disc = new_test_discover();
        disc.ciaddr = random_ipv4_generator();
        assert_eq!(
            validate_discover(&disc),
            Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
                field: String::from("ciaddr"),
                value: disc.ciaddr.to_string(),
                msg_type: MessageType::DHCPDISCOVER
            }))
        );
        disc = new_test_discover();
        disc.yiaddr = random_ipv4_generator();
        assert_eq!(
            validate_discover(&disc),
            Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
                field: String::from("yiaddr"),
                value: disc.yiaddr.to_string(),
                msg_type: MessageType::DHCPDISCOVER
            }))
        );
        disc = new_test_discover();
        disc.siaddr = random_ipv4_generator();
        assert_eq!(
            validate_discover(&disc),
            Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
                field: String::from("siaddr"),
                value: disc.siaddr.to_string(),
                msg_type: MessageType::DHCPDISCOVER
            }))
        );
        disc = new_test_discover();
        let server = random_ipv4_generator();
        let () = disc.options.push(DhcpOption::ServerIdentifier(server));
        assert_eq!(
            validate_discover(&disc),
            Err(ServerError::ClientMessageError(ProtocolError::InvalidField {
                field: String::from("ServerIdentifier"),
                value: server.to_string(),
                msg_type: MessageType::DHCPDISCOVER
            }))
        );
        disc = new_test_discover();
        assert_eq!(validate_discover(&disc), Ok(()));
    }

    #[test]
    fn test_build_offer_with_custom_t1_t2() {
        let mut server = new_test_minimal_server();
        let initial_disc = new_test_discover();
        let initial_offer_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(initial_offer_ip));
        let offer =
            server.build_offer(initial_disc, initial_offer_ip).expect("failed to build offer");
        let v = offer.options.iter().find_map(|v| match v {
            DhcpOption::RenewalTimeValue(v) => Some(*v),
            _ => None,
        });
        assert_eq!(
            v,
            Some(server.params.lease_length.default_seconds / 2),
            "offer options did not contain expected renewal time: {:?}",
            offer.options
        );
        let v = offer.options.iter().find_map(|v| match v {
            DhcpOption::RebindingTimeValue(v) => Some(*v),
            _ => None,
        });
        assert_eq!(
            v,
            Some((server.params.lease_length.default_seconds * 3) / 4),
            "offer options did not contain expected rebinding time: {:?}",
            offer.options
        );
        let t1 = rand::random::<u32>();
        assert_matches::assert_matches!(
            server
                .options_repo
                .insert(OptionCode::RenewalTimeValue, DhcpOption::RenewalTimeValue(t1)),
            None
        );
        let t2 = rand::random::<u32>();
        assert_matches::assert_matches!(
            server
                .options_repo
                .insert(OptionCode::RebindingTimeValue, DhcpOption::RebindingTimeValue(t2)),
            None
        );
        let disc = new_test_discover();
        let offer_ip = random_ipv4_generator();
        assert!(server.pool.universe.insert(offer_ip));
        let offer = server.build_offer(disc, offer_ip).expect("failed to build offer");
        let v = offer.options.iter().find_map(|v| match v {
            DhcpOption::RenewalTimeValue(v) => Some(*v),
            _ => None,
        });
        assert_eq!(
            v,
            Some(t1),
            "offer options did not contain expected renewal time: {:?}",
            offer.options
        );
        let v = offer.options.iter().find_map(|v| match v {
            DhcpOption::RebindingTimeValue(v) => Some(*v),
            _ => None,
        });
        assert_eq!(
            v,
            Some(t2),
            "offer options did not contain expected rebinding time: {:?}",
            offer.options
        );
    }
}
