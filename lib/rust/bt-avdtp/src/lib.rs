// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(arbitrary_self_types, async_await, await_macro, futures_api, pin)]

#[macro_use]
extern crate failure;

use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_info, fx_log_warn, fx_vlog};
use fuchsia_zircon::{self as zx, Duration, Time};
use futures::future::FusedFuture;
use futures::ready;
use futures::select;
use futures::stream::Stream;
use futures::task::{LocalWaker, Poll, Waker};
use futures::FutureExt;
use parking_lot::Mutex;
use slab::Slab;
use std::collections::VecDeque;
use std::fmt;
use std::io::{Cursor, Write};
use std::mem;
use std::pin::{Pin, Unpin};
use std::sync::Arc;

#[cfg(test)]
mod tests;

mod types;

use crate::types::{
    Decodable, Encodable, ErrorCode, SignalIdentifier, SignalingHeader, SignalingMessageType,
    TryFrom, TxLabel,
};

pub use crate::types::{EndpointType, Error, MediaType};

/// An AVDTP signaling peer can send commands to another peer, receive requests and send responses.
/// Media transport is not handled by this peer.
///
/// Requests from the distant peer are delivered through the request stream available through
/// take_request_stream().  Only one RequestStream can be active at a time.  Only valid requests
/// are sent to the request stream - invalid formats are automatically rejected.
///
/// Responses are sent using responders that are included in the request stream from the connected
/// peer.
#[derive(Debug)]
pub struct Peer {
    inner: Arc<PeerInner>,
}

impl Peer {
    /// Create a new peer from a signaling channel socket.
    pub fn new(signaling: zx::Socket) -> Result<Peer, zx::Status> {
        Ok(Peer {
            inner: Arc::new(PeerInner {
                signaling: fasync::Socket::from_socket(signaling)?,
                response_waiters: Mutex::new(Slab::<ResponseWaiter>::new()),
                incoming_requests: Mutex::<RequestQueue>::default(),
            }),
        })
    }

    /// Take the event listener for this peer. Panics if the stream is already
    /// held.
    pub fn take_request_stream(&self) -> RequestStream {
        {
            let mut lock = self.inner.incoming_requests.lock();
            if let RequestListener::None = lock.listener {
                lock.listener = RequestListener::New;
            } else {
                panic!("Request stream has already been taken");
            }
        }

        RequestStream {
            inner: self.inner.clone(),
        }
    }

    /// Send a Stream End Point Discovery (Sec 8.6) command to the remote peer.
    /// Asynchronously returns a the reply in a vector of endpoint information.
    /// Error will be RemoteRejected with the error code returned by the remote
    /// if the remote peer rejected the command.
    pub async fn discover(&self) -> Result<Vec<StreamInformation>, Error> {
        let response: Result<DiscoverResponse, Error> =
            await!(self.send_command(SignalIdentifier::Discover, &[]));
        match response {
            Ok(response) => Ok(response.endpoints),
            Err(e) => Err(e),
        }
    }

    /// Send a Get Capabilities (Sec 8.7) command to the remote peer for the
    /// given `stream_id`.
    /// Asynchronously returns the reply which contains the ServiceCapabilities
    /// reported.
    /// In general, Get All Capabilities should be preferred to this command.
    /// Error will be RemoteRejected with the error code reported by the remote
    /// if the remote peer rejects the command.
    pub async fn get_capabilities<'a>(
        &'a self, stream_id: &'a StreamEndpointId,
    ) -> Result<Vec<ServiceCapability>, Error> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<GetCapabilitiesResponse, Error> =
            await!(self.send_command(SignalIdentifier::GetCapabilities, stream_params));
        match response {
            Ok(response) => Ok(response.capabilities),
            Err(e) => Err(e),
        }
    }

    /// Send a Get All Capabilities (Sec 8.8) command to the remote peer for the
    /// given `stream_id`.
    /// Asynchronously returns the reply which contains the ServiceCapabilities
    /// reported.
    /// Error will be RemoteRejected with the error code reported by the remote
    /// if the remote peer rejects the command.
    pub async fn get_all_capabilities<'a>(
        &'a self, stream_id: &'a StreamEndpointId,
    ) -> Result<Vec<ServiceCapability>, Error> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<GetCapabilitiesResponse, Error> =
            await!(self.send_command(SignalIdentifier::GetAllCapabilities, stream_params));
        match response {
            Ok(response) => Ok(response.capabilities),
            Err(e) => Err(e),
        }
    }

    /// Send a Open Stream Command (Sec 8.12) to the remote peer for the given
    /// `stream_id`.
    /// Returns Ok(()) if the command is accepted, and RemoteRejected if the
    /// remote peer rejects the command with the code returned by the remote.
    pub async fn open<'a>(&'a self, stream_id: &'a StreamEndpointId) -> Result<(), Error> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<SimpleResponse, Error> =
            await!(self.send_command(SignalIdentifier::Open, stream_params));
        response?;
        Ok(())
    }

    /// Send a Start Stream Command (Sec 8.13) to the remote peer for all the
    /// streams in `stream_ids`.
    /// Returns Ok(()) if the command is accepted, and RemoteStreamRejected
    /// with the stream endpoint id and error code reported by the remote if
    /// the remote signals a failure.
    pub async fn start<'a>(&'a self, stream_ids: &'a [StreamEndpointId]) -> Result<(), Error> {
        let mut stream_params = Vec::with_capacity(stream_ids.len());
        for stream_id in stream_ids {
            stream_params.push(stream_id.to_msg());
        }
        let response: Result<SimpleResponse, Error> =
            await!(self.send_command(SignalIdentifier::Start, &stream_params));
        response?;
        Ok(())
    }

    /// Send a Close Stream Command (Sec 8.14) to the remote peer for the given
    /// `stream_id`.
    /// Returns Ok(()) if the command is accepted, and RemoteRejected if the
    /// remote peer rejects the command with the code returned by the remote.
    pub async fn close<'a>(&'a self, stream_id: &'a StreamEndpointId) -> Result<(), Error> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<SimpleResponse, Error> =
            await!(self.send_command(SignalIdentifier::Close, stream_params));
        response?;
        Ok(())
    }

    /// Send a Suspend Command (Sec 8.15) to the remote peer for all the
    /// streams in `stream_ids`.
    /// Returns Ok(()) if the command is accepted, and RemoteStreamRejected
    /// with the stream endpoint id and error code reported by the remote if
    /// the remote signals a failure.
    pub async fn suspend<'a>(&'a self, stream_ids: &'a [StreamEndpointId]) -> Result<(), Error> {
        let mut stream_params = Vec::with_capacity(stream_ids.len());
        for stream_id in stream_ids {
            stream_params.push(stream_id.to_msg());
        }
        let response: Result<SimpleResponse, Error> =
            await!(self.send_command(SignalIdentifier::Suspend, &stream_params));
        response?;
        Ok(())
    }

    /// Send a Close Stream Command (Sec 8.14) to the remote peer for the given
    /// `stream_id`.
    /// Returns Ok(()) if the command is accepted, and RemoteRejected if the
    /// remote peer rejects the command with the code returned by the remote.
    pub async fn abort<'a>(&'a self, stream_id: &'a StreamEndpointId) -> Result<(), Error> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<SimpleResponse, Error> =
            await!(self.send_command(SignalIdentifier::Abort, stream_params));
        response?;
        Ok(())
    }

    /// The maximum amount of time we will wait for a response to a signaling command.
    fn command_timeout() -> Duration {
        const RTX_SIG_TIMER_MS: i64 = 3000;
        Duration::from_millis(RTX_SIG_TIMER_MS)
    }

    /// Sends a signal on the socket and receive a future that will complete
    /// when we get the expected reponse.
    async fn send_command<'a, D: Decodable>(
        &'a self, signal: SignalIdentifier, payload: &'a [u8],
    ) -> Result<D, Error> {
        let id = self.inner.add_response_waiter()?;
        let header = SignalingHeader::new(id, signal, SignalingMessageType::Command);

        {
            let mut buf = vec![0; header.encoded_len()];

            header.encode(buf.as_mut_slice())?;
            buf.extend_from_slice(payload);

            self.inner.send_signal(buf.as_slice())?;
        }

        let mut response = CommandResponse {
            id: header.label(),
            inner: Some(self.inner.clone()),
        };

        let mut timeout = fasync::Timer::new(Time::after(Peer::command_timeout())).fuse();

        select! {
            _ = timeout => Err(Error::Timeout),
            r = response => {
                let response_buf = r?;
                decode_signaling_response(header.signal(), response_buf)
            },
        }
    }
}

/// A request from the connected peer.
/// Each variant of this includes a responder which implements two functions:
///  - send(...) will send a response with the information provided.
///  - reject(ErrorCode) will send an reject response with the given error code.
#[derive(Debug)]
pub enum Request {
    Discover {
        responder: DiscoverResponder,
    },
    GetCapabilities {
        stream_id: StreamEndpointId,
        responder: GetCapabilitiesResponder,
    },
    GetAllCapabilities {
        stream_id: StreamEndpointId,
        responder: GetCapabilitiesResponder,
    },
    SetConfiguration {
        local_stream_id: StreamEndpointId,
        remote_stream_id: StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
        responder: ConfigureResponder,
    },
    Open {
        stream_id: StreamEndpointId,
        responder: SimpleResponder,
    },
    Reconfigure {
        local_stream_id: StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
        responder: ConfigureResponder,
    },
    Start {
        stream_ids: Vec<StreamEndpointId>,
        responder: StreamResponder,
    },
    Close {
        stream_id: StreamEndpointId,
        responder: SimpleResponder,
    },
    Suspend {
        stream_ids: Vec<StreamEndpointId>,
        responder: StreamResponder,
    },
    Abort {
        stream_id: StreamEndpointId,
        responder: SimpleResponder,
    },
    // TODO(jamuraa): add the rest of the requests
}

macro_rules! parse_one_seid {
    ($body:ident, $signal:ident, $peer:ident, $id:ident, $request_variant:ident, $responder_type:ident) => {
        if $body.len() != 1 {
            Err(Error::RequestInvalid(ErrorCode::BadLength))
        } else {
            Ok(Request::$request_variant {
                stream_id: StreamEndpointId::from_msg(&$body[0]),
                responder: $responder_type {
                    signal: $signal,
                    peer: $peer,
                    id: $id,
                },
            })
        }
    };
}

impl Request {
    fn get_req_seids(body: &[u8]) -> Result<Vec<StreamEndpointId>, Error> {
        if body.len() < 1 {
            return Err(Error::RequestInvalid(ErrorCode::BadLength));
        }
        Ok(body.iter().map(&StreamEndpointId::from_msg).collect())
    }

    fn get_req_capabilities(encoded: &[u8]) -> Result<Vec<ServiceCapability>, Error> {
        if encoded.len() < 2 {
            return Err(Error::RequestInvalid(ErrorCode::BadLength));
        }
        let mut caps = vec![];
        let mut loc = 0;
        while loc < encoded.len() {
            let cap = match ServiceCapability::decode(&encoded[loc..]) {
                Ok(cap) => cap,
                Err(Error::RequestInvalid(code)) => {
                    return Err(Error::RequestInvalidExtra(code, encoded[loc]))
                }
                Err(e) => return Err(e),
            };
            loc += cap.encoded_len();
            caps.push(cap);
        }
        Ok(caps)
    }

    fn parse(
        peer: Arc<PeerInner>, id: TxLabel, signal: SignalIdentifier, body: &[u8],
    ) -> Result<Request, Error> {
        match signal {
            SignalIdentifier::Discover => {
                // Discover Request has no body (Sec 8.6.1)
                if body.len() > 0 {
                    return Err(Error::RequestInvalid(ErrorCode::BadLength));
                }
                Ok(Request::Discover {
                    responder: DiscoverResponder { peer: peer, id: id },
                })
            }
            SignalIdentifier::GetCapabilities => parse_one_seid!(
                body,
                signal,
                peer,
                id,
                GetCapabilities,
                GetCapabilitiesResponder
            ),
            SignalIdentifier::GetAllCapabilities => parse_one_seid!(
                body,
                signal,
                peer,
                id,
                GetAllCapabilities,
                GetCapabilitiesResponder
            ),
            SignalIdentifier::SetConfiguration => {
                if body.len() < 4 {
                    return Err(Error::RequestInvalid(ErrorCode::BadLength));
                }
                let requested = Request::get_req_capabilities(&body[2..])?;
                Ok(Request::SetConfiguration {
                    local_stream_id: StreamEndpointId::from_msg(&body[0]),
                    remote_stream_id: StreamEndpointId::from_msg(&body[1]),
                    capabilities: requested,
                    responder: ConfigureResponder { signal, peer, id },
                })
            }
            SignalIdentifier::Reconfigure => {
                if body.len() < 3 {
                    return Err(Error::RequestInvalid(ErrorCode::BadLength));
                }
                let requested = Request::get_req_capabilities(&body[1..])?;
                match requested.iter().find(|x| !x.is_application()) {
                    Some(x) => {
                        return Err(Error::RequestInvalidExtra(
                            ErrorCode::InvalidCapabilities,
                            x.to_category_byte(),
                        ))
                    }
                    None => (),
                };
                Ok(Request::Reconfigure {
                    local_stream_id: StreamEndpointId::from_msg(&body[0]),
                    capabilities: requested,
                    responder: ConfigureResponder { signal, peer, id },
                })
            }
            SignalIdentifier::Open => {
                parse_one_seid!(body, signal, peer, id, Open, SimpleResponder)
            }
            SignalIdentifier::Start => {
                let seids = Request::get_req_seids(body)?;
                Ok(Request::Start {
                    stream_ids: seids,
                    responder: StreamResponder { signal, peer, id },
                })
            }
            SignalIdentifier::Close => {
                parse_one_seid!(body, signal, peer, id, Close, SimpleResponder)
            }
            SignalIdentifier::Suspend => {
                let seids = Request::get_req_seids(body)?;
                Ok(Request::Suspend {
                    stream_ids: seids,
                    responder: StreamResponder { signal, peer, id },
                })
            }
            SignalIdentifier::Abort => {
                parse_one_seid!(body, signal, peer, id, Abort, SimpleResponder)
            }
            _ => Err(Error::UnimplementedMessage),
        }
    }
}

/// A stream of requests from the remote peer.
#[derive(Debug)]
pub struct RequestStream {
    inner: Arc<PeerInner>,
}

impl Unpin for RequestStream {}

impl Stream for RequestStream {
    type Item = Result<Request, Error>;

    fn poll_next(self: Pin<&mut Self>, lw: &LocalWaker) -> Poll<Option<Self::Item>> {
        Poll::Ready(match ready!(self.inner.poll_recv_request(lw)) {
            Ok(UnparsedRequest(SignalingHeader { label, signal, .. }, body)) => {
                match Request::parse(self.inner.clone(), label, signal, &body) {
                    Err(Error::RequestInvalid(code)) => {
                        self.inner.send_reject(label, signal, code)?;
                        return Poll::Pending;
                    }
                    Err(Error::RequestInvalidExtra(code, extra)) => {
                        self.inner
                            .send_reject_params(label, signal, &[extra, u8::from(&code)])?;
                        return Poll::Pending;
                    }
                    Err(Error::UnimplementedMessage) => {
                        self.inner
                            .send_reject(label, signal, ErrorCode::NotSupportedCommand)?;
                        return Poll::Pending;
                    }
                    x => Some(x),
                }
            }
            Err(Error::PeerDisconnected) => None,
            Err(e) => Some(Err(e)),
        })
    }
}

impl Drop for RequestStream {
    fn drop(&mut self) {
        self.inner.incoming_requests.lock().listener = RequestListener::None;
        self.inner.wake_any();
    }
}

/// A Stream Endpoint Identifier, aka SEID, INT SEID, ACP SEID - Sec 8.20.1
/// Valid values are 0x01 - 0x3E
#[derive(Debug, PartialEq)]
pub struct StreamEndpointId(u8);

impl StreamEndpointId {
    /// Interpret a StreamEndpointId from the upper six bits of a byte, which
    /// is often how it's transmitted in a message.
    fn from_msg(byte: &u8) -> Self {
        StreamEndpointId(byte >> 2)
    }

    /// Produce a byte where the SEID value is placed in the upper six bits,
    /// which is often how it is placed in a mesage.
    fn to_msg(&self) -> u8 {
        self.0 << 2
    }
}

impl TryFrom<u8> for StreamEndpointId {
    type Error = Error;
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        if value == 0 || value > 0x3E {
            Err(Error::OutOfRange)
        } else {
            Ok(StreamEndpointId(value))
        }
    }
}

impl fmt::Display for StreamEndpointId {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "{}", self.0)
    }
}

/// All information related to a stream. Part of the Discovery Response.
/// See Sec 8.6.2
#[derive(Debug, PartialEq)]
pub struct StreamInformation {
    id: StreamEndpointId,
    in_use: bool,
    media_type: MediaType,
    endpoint_type: EndpointType,
}

impl StreamInformation {
    /// Create a new StreamInformation from an ID.
    /// This will only fail if the ID given is out of the range of valid SEIDs (0x01 - 0x3E)
    pub fn new(
        id: u8, in_use: bool, media_type: MediaType, endpoint_type: EndpointType,
    ) -> Result<StreamInformation, Error> {
        Ok(StreamInformation {
            id: StreamEndpointId::try_from(id)?,
            in_use: in_use,
            media_type: media_type,
            endpoint_type: endpoint_type,
        })
    }

    pub fn id(&self) -> &StreamEndpointId {
        &self.id
    }
}

impl Decodable for StreamInformation {
    fn decode(from: &[u8]) -> Result<Self, Error> {
        if from.len() < 2 {
            return Err(Error::InvalidMessage);
        }
        let id = StreamEndpointId::from_msg(&from[0]);
        let in_use: bool = from[0] & 0x02 != 0;
        let media_type = MediaType::try_from(from[1] >> 4)?;
        let endpoint_type = EndpointType::try_from((from[1] >> 3) & 0x1)?;
        Ok(StreamInformation {
            id: id,
            in_use: in_use,
            media_type: media_type,
            endpoint_type: endpoint_type,
        })
    }
}

impl Encodable for StreamInformation {
    fn encoded_len(&self) -> usize {
        2
    }

    fn encode(&self, into: &mut [u8]) -> Result<(), Error> {
        if into.len() < self.encoded_len() {
            return Err(Error::Encoding);
        }
        into[0] = self.id.to_msg() | if self.in_use { 0x02 } else { 0x00 };
        into[1] = u8::from(&self.media_type) << 4 | u8::from(&self.endpoint_type) << 3;
        Ok(())
    }
}

// Simple responses have no body data.
#[derive(Debug)]
pub struct SimpleResponse {}

impl Decodable for SimpleResponse {
    fn decode(from: &[u8]) -> Result<Self, Error> {
        if from.len() > 0 {
            return Err(Error::InvalidMessage);
        }
        Ok(SimpleResponse {})
    }
}

#[derive(Debug)]
struct DiscoverResponse {
    endpoints: Vec<StreamInformation>,
}

impl Decodable for DiscoverResponse {
    fn decode(from: &[u8]) -> Result<Self, Error> {
        let mut endpoints = Vec::<StreamInformation>::new();
        let mut idx = 0;
        while idx < from.len() {
            let endpoint = StreamInformation::decode(&from[idx..])?;
            idx += endpoint.encoded_len();
            endpoints.push(endpoint);
        }
        Ok(DiscoverResponse {
            endpoints: endpoints,
        })
    }
}

#[derive(Debug)]
pub struct DiscoverResponder {
    peer: Arc<PeerInner>,
    id: TxLabel,
}

impl DiscoverResponder {
    /// Sends the response to a discovery request.
    /// At least one endpoint must be present.
    /// Will result in a Error::PeerWrite if the distant peer is disconnected.
    pub fn send(self, endpoints: &[StreamInformation]) -> Result<(), Error> {
        if endpoints.len() == 0 {
            // There shall be at least one SEP in a response (Sec 8.6.2)
            return Err(Error::Encoding);
        }
        let mut params = vec![0 as u8; endpoints.len() * endpoints[0].encoded_len()];
        let mut idx = 0;
        for endpoint in endpoints {
            endpoint.encode(&mut params[idx..idx + endpoint.encoded_len()])?;
            idx += endpoint.encoded_len();
        }
        self.peer
            .send_response(self.id, SignalIdentifier::Discover, &params)
    }

    pub fn reject(self, error_code: ErrorCode) -> Result<(), Error> {
        self.peer
            .send_reject(self.id, SignalIdentifier::Discover, error_code)
    }
}

#[derive(Debug)]
pub struct GetCapabilitiesResponder {
    peer: Arc<PeerInner>,
    signal: SignalIdentifier,
    id: TxLabel,
}

impl GetCapabilitiesResponder {
    pub fn send(self, capabilities: &[ServiceCapability]) -> Result<(), Error> {
        let included_iter = capabilities.iter().filter(|x| x.in_response(self.signal));
        let reply_len = included_iter.clone().fold(0, |a, b| a + b.encoded_len());
        let mut reply = vec![0 as u8; reply_len];
        let mut pos = 0;
        for capability in included_iter {
            let size = capability.encoded_len();
            capability.encode(&mut reply[pos..pos + size])?;
            pos += size;
        }
        self.peer.send_response(self.id, self.signal, &reply)
    }

    pub fn reject(self, error_code: ErrorCode) -> Result<(), Error> {
        self.peer.send_reject(self.id, self.signal, error_code)
    }
}

#[derive(Debug)]
struct GetCapabilitiesResponse {
    capabilities: Vec<ServiceCapability>,
}

impl Decodable for GetCapabilitiesResponse {
    fn decode(from: &[u8]) -> Result<Self, Error> {
        let mut capabilities = Vec::<ServiceCapability>::new();
        let mut idx = 0;
        while idx < from.len() {
            let capability = ServiceCapability::decode(&from[idx..])?;
            idx = idx + capability.encoded_len();
            capabilities.push(capability);
        }
        Ok(GetCapabilitiesResponse {
            capabilities: capabilities,
        })
    }
}

/// The type of the codec in the MediaCodec Service Capability
/// Valid values are defined in the Bluetooth Assigned Numbers and are
/// interpreted differently for different Media Types, so we do not interpret
/// them here.
/// See https://www.bluetooth.com/specifications/assigned-numbers/audio-video
#[derive(Debug, PartialEq)]
pub struct MediaCodecType(u8);

impl MediaCodecType {
    pub fn new(num: u8) -> MediaCodecType {
        MediaCodecType(num)
    }
}

/// The type of content protection used in the Content Protection Service Capability.
/// Defined in the Bluetooth Assigned Numbers
/// https://www.bluetooth.com/specifications/assigned-numbers/audio-video
#[derive(Debug, PartialEq)]
pub enum ContentProtectionType {
    DigitalTransmissionContentProtection, // DTCP, 0x0001
    SerialCopyManagementSystem,           // SCMS-T, 0x0002
}

impl ContentProtectionType {
    fn to_le_bytes(&self) -> [u8; 2] {
        match self {
            ContentProtectionType::DigitalTransmissionContentProtection => [0x01, 0x00],
            ContentProtectionType::SerialCopyManagementSystem => [0x02, 0x00],
        }
    }
}

impl TryFrom<u16> for ContentProtectionType {
    type Error = Error;

    fn try_from(val: u16) -> Result<Self, Self::Error> {
        match val {
            1 => Ok(ContentProtectionType::DigitalTransmissionContentProtection),
            2 => Ok(ContentProtectionType::SerialCopyManagementSystem),
            _ => Err(Error::OutOfRange),
        }
    }
}

/// Service Capabilities indicate possible services that can be provided by
/// each stream endpoint.  See AVDTP Spec section 8.21.
#[derive(Debug, PartialEq)]
pub enum ServiceCapability {
    /// Indicates that the end point can provide at least basic media transport
    /// service as defined by RFC 3550 and outlined in section 7.2.
    /// Defined in section 8.21.2
    MediaTransport,
    /// Indicates that the end point can provide reporting service as outlined in section 7.3
    /// Defined in section 8.21.3
    Reporting,
    /// Indicates the end point can provide recovery service as outlined in section 7.4
    /// Defined in section 8.21.4
    Recovery {
        recovery_type: u8,
        max_recovery_window_size: u8,
        max_number_media_packets: u8,
    },
    /// Indicates the codec which is supported by this end point. |codec_extra| is defined within
    /// the relevant profiles (A2DP for Audio, etc).
    /// Defined in section 8.21.5
    MediaCodec {
        media_type: MediaType,
        codec_type: MediaCodecType,
        codec_extra: Vec<u8>, // TODO: Media codec specific information elements
    },
    /// Present when the device has content protection capability.
    /// |extra| is defined elsewhere.
    /// Defined in section 8.21.6
    ContentProtection {
        protection_type: ContentProtectionType,
        extra: Vec<u8>, // Protection speciifc parameters
    },
    /// Indicates that delay reporting is offered by this end point.
    /// Defined in section 8.21.9
    DelayReporting,
}

impl ServiceCapability {
    fn to_category_byte(&self) -> u8 {
        match self {
            ServiceCapability::MediaTransport => 1,
            ServiceCapability::Reporting => 2,
            ServiceCapability::Recovery { .. } => 3,
            ServiceCapability::ContentProtection { .. } => 4,
            ServiceCapability::MediaCodec { .. } => 7,
            ServiceCapability::DelayReporting => 8,
        }
    }

    fn length_of_service_capabilities(&self) -> u8 {
        match self {
            ServiceCapability::MediaTransport => 0,
            ServiceCapability::Reporting => 0,
            ServiceCapability::Recovery { .. } => 3,
            ServiceCapability::MediaCodec { codec_extra, .. } => 2 + codec_extra.len() as u8,
            ServiceCapability::ContentProtection { extra, .. } => 2 + extra.len() as u8,
            ServiceCapability::DelayReporting => 0,
        }
    }

    /// True when this ServiceCapability is a "basic" capability.
    /// See Table 8.47 in Section 8.21.1
    fn is_basic(&self) -> bool {
        match self {
            ServiceCapability::DelayReporting => false,
            _ => true,
        }
    }

    /// True when this capability should be included in the response to a |sig| command.
    fn in_response(&self, sig: SignalIdentifier) -> bool {
        sig == SignalIdentifier::GetAllCapabilities || self.is_basic()
    }

    /// True when this capability is classified as an "Application Service Capability"
    fn is_application(&self) -> bool {
        match self {
            ServiceCapability::MediaCodec { .. } | ServiceCapability::ContentProtection { .. } => {
                true
            }
            _ => false,
        }
    }
}

impl Decodable for ServiceCapability {
    fn decode(from: &[u8]) -> Result<Self, Error> {
        if from.len() < 2 {
            return Err(Error::Encoding);
        }
        let length_of_capability = from[1] as usize;
        let d = match from[0] {
            1 => match length_of_capability {
                0 => ServiceCapability::MediaTransport,
                _ => return Err(Error::RequestInvalid(ErrorCode::BadMediaTransportFormat)),
            },
            2 => match length_of_capability {
                0 => ServiceCapability::Reporting,
                _ => return Err(Error::RequestInvalid(ErrorCode::BadPayloadFormat)),
            },
            3 => {
                if from.len() < 5 || length_of_capability != 3 {
                    return Err(Error::RequestInvalid(ErrorCode::BadRecoveryFormat));
                }
                let recovery_type = from[2];
                let mrws = from[3];
                let mnmp = from[4];
                // Check format of parameters. See Section 8.21.4, Table 8.51
                // The only recovery type is RFC2733 (0x01)
                if recovery_type != 0x01 {
                    return Err(Error::RequestInvalid(ErrorCode::BadRecoveryType));
                }
                // The MRWS and MNMP must be 0x01 - 0x18
                if mrws < 0x01 || mrws > 0x18 || mnmp < 0x01 || mnmp > 0x18 {
                    return Err(Error::RequestInvalid(ErrorCode::BadRecoveryFormat));
                }
                ServiceCapability::Recovery {
                    recovery_type,
                    max_recovery_window_size: mrws,
                    max_number_media_packets: mnmp,
                }
            }
            4 => {
                let cp_format_err = Err(Error::RequestInvalid(ErrorCode::BadCpFormat));
                if from.len() < 4
                    || length_of_capability < 2
                    || from.len() < length_of_capability + 2
                {
                    return cp_format_err;
                }
                let cp_type: u16 = ((from[3] as u16) << 8) + from[2] as u16;
                let protection_type = match ContentProtectionType::try_from(cp_type) {
                    Ok(val) => val,
                    Err(_) => return cp_format_err,
                };
                let extra_len = length_of_capability - 2;
                let mut extra = vec![0; extra_len];
                extra.copy_from_slice(&from[4..4 + extra_len]);
                ServiceCapability::ContentProtection {
                    protection_type,
                    extra,
                }
            }
            7 => {
                let format_err = Err(Error::RequestInvalid(ErrorCode::BadPayloadFormat));
                if from.len() < 4
                    || length_of_capability < 2
                    || from.len() < length_of_capability + 2
                {
                    return format_err;
                }
                let media_type = match MediaType::try_from(from[2] >> 4) {
                    Ok(media) => media,
                    Err(_) => return format_err,
                };
                let codec_type = MediaCodecType::new(from[3]);
                let extra_len = length_of_capability - 2;
                let mut codec_extra = vec![0; extra_len];
                codec_extra.copy_from_slice(&from[4..4 + extra_len]);
                ServiceCapability::MediaCodec {
                    media_type,
                    codec_type,
                    codec_extra,
                }
            }
            8 => match length_of_capability {
                0 => ServiceCapability::DelayReporting,
                _ => return Err(Error::RequestInvalid(ErrorCode::BadPayloadFormat)),
            },
            _ => {
                return Err(Error::RequestInvalid(ErrorCode::BadServiceCategory));
            }
        };
        Ok(d)
    }
}

impl Encodable for ServiceCapability {
    fn encoded_len(&self) -> usize {
        2 + self.length_of_service_capabilities() as usize
    }

    fn encode(&self, buf: &mut [u8]) -> Result<(), Error> {
        if buf.len() < self.encoded_len() {
            return Err(Error::Encoding);
        }
        let mut cursor = Cursor::new(buf);
        cursor
            .write(&[
                self.to_category_byte(),
                self.length_of_service_capabilities(),
            ])
            .map_err(|_| Error::Encoding)?;
        match self {
            ServiceCapability::Recovery {
                recovery_type: t,
                max_recovery_window_size: max_size,
                max_number_media_packets: max_packets,
            } => {
                cursor
                    .write(&[*t, *max_size, *max_packets])
                    .map_err(|_| Error::Encoding)?;
            }
            ServiceCapability::MediaCodec {
                media_type,
                codec_type,
                codec_extra,
            } => {
                cursor
                    .write(&[u8::from(media_type) << 4, codec_type.0])
                    .map_err(|_| Error::Encoding)?;
                cursor
                    .write(codec_extra.as_slice())
                    .map_err(|_| Error::Encoding)?;
            }
            ServiceCapability::ContentProtection {
                protection_type,
                extra,
            } => {
                cursor
                    .write(&protection_type.to_le_bytes())
                    .map_err(|_| Error::Encoding)?;
                cursor
                    .write(extra.as_slice())
                    .map_err(|_| Error::Encoding)?;
            }
            _ => {}
        }
        Ok(())
    }
}

#[derive(Debug)]
pub struct SimpleResponder {
    peer: Arc<PeerInner>,
    signal: SignalIdentifier,
    id: TxLabel,
}

impl SimpleResponder {
    pub fn send(self) -> Result<(), Error> {
        self.peer.send_response(self.id, self.signal, &[])
    }

    pub fn reject(self, error_code: ErrorCode) -> Result<(), Error> {
        self.peer.send_reject(self.id, self.signal, error_code)
    }
}

#[derive(Debug)]
pub struct StreamResponder {
    peer: Arc<PeerInner>,
    signal: SignalIdentifier,
    id: TxLabel,
}

impl StreamResponder {
    pub fn send(self) -> Result<(), Error> {
        self.peer.send_response(self.id, self.signal, &[])
    }

    pub fn reject(self, stream_id: &StreamEndpointId, error_code: ErrorCode) -> Result<(), Error> {
        self.peer.send_reject_params(
            self.id,
            self.signal,
            &[stream_id.to_msg(), u8::from(&error_code)],
        )
    }
}

#[derive(Debug)]
pub struct ConfigureResponder {
    peer: Arc<PeerInner>,
    signal: SignalIdentifier,
    id: TxLabel,
}

impl ConfigureResponder {
    pub fn send(self) -> Result<(), Error> {
        self.peer.send_response(self.id, self.signal, &[])
    }

    pub fn reject(
        self, capability: Option<&ServiceCapability>, error_code: ErrorCode,
    ) -> Result<(), Error> {
        let service_byte: u8 = match capability {
            None => 0x0, // If no service category applies, see notes in Sec 8.11.3 or 8.9.3
            Some(cap) => cap.to_category_byte(),
        };
        self.peer
            .send_reject_params(self.id, self.signal, &[service_byte, u8::from(&error_code)])
    }
}

#[derive(Debug)]
struct UnparsedRequest(SignalingHeader, Vec<u8>);

impl UnparsedRequest {
    fn new(header: SignalingHeader, body: Vec<u8>) -> UnparsedRequest {
        UnparsedRequest(header, body)
    }
}

#[derive(Debug, Default)]
struct RequestQueue {
    listener: RequestListener,
    queue: VecDeque<UnparsedRequest>,
}

#[derive(Debug)]
enum RequestListener {
    /// No one is listening.
    None,
    /// Someone wants to listen but hasn't polled.
    New,
    /// Someone is listening, and can be woken whith the waker.
    Some(Waker),
}

impl Default for RequestListener {
    fn default() -> Self {
        RequestListener::None
    }
}

/// An enum representing an interest in the response to a command.
#[derive(Debug)]
enum ResponseWaiter {
    /// A new waiter which hasn't been polled yet.
    WillPoll,
    /// A task waiting for a response, which can be woken with the waker.
    Waiting(Waker),
    /// A response that has been received, stored here until it's polled, at
    /// which point it will be decoded.
    Received(Vec<u8>),
    /// It's still waiting on the reponse, but the receiver has decided they
    /// don't care and we'll throw it out.
    Discard,
}

impl ResponseWaiter {
    /// Check if a message has been received.
    fn is_received(&self) -> bool {
        if let ResponseWaiter::Received(_) = self {
            true
        } else {
            false
        }
    }

    fn unwrap_received(self) -> Vec<u8> {
        if let ResponseWaiter::Received(buf) = self {
            buf
        } else {
            panic!("expected received buf")
        }
    }
}

fn decode_signaling_response<D: Decodable>(
    expected_signal: SignalIdentifier, buf: Vec<u8>,
) -> Result<D, Error> {
    let header = SignalingHeader::decode(buf.as_slice())?;
    if header.signal() != expected_signal {
        return Err(Error::InvalidHeader);
    }
    if !header.is_type(SignalingMessageType::ResponseAccept) {
        match header.signal() {
            SignalIdentifier::Start | SignalIdentifier::Suspend => {
                return Err(Error::RemoteStreamRejected(
                    buf[header.encoded_len()] >> 2,
                    buf[header.encoded_len() + 1],
                ));
            }
            _ => return Err(Error::RemoteRejected(buf[header.encoded_len()])),
        };
    }
    D::decode(&buf[header.encoded_len()..])
}

/// A future that polls for the response to a command we sent.
#[derive(Debug)]
pub struct CommandResponse {
    id: TxLabel,
    // Some(x) if we're still waiting on the response.
    inner: Option<Arc<PeerInner>>,
}

impl Unpin for CommandResponse {}

impl futures::Future for CommandResponse {
    type Output = Result<Vec<u8>, Error>;
    fn poll(mut self: Pin<&mut Self>, lw: &LocalWaker) -> Poll<Self::Output> {
        let this = &mut *self;
        let res;
        {
            let client = this.inner.as_ref().ok_or(Error::AlreadyReceived)?;
            res = client.poll_recv_response(&this.id, lw);
        }

        if let Poll::Ready(Ok(_)) = res {
            let inner = this
                .inner
                .take()
                .expect("CommandResponse polled after completion");
            inner.wake_any();
        }

        res
    }
}

impl FusedFuture for CommandResponse {
    fn is_terminated(&self) -> bool {
        self.inner.is_none()
    }
}

impl Drop for CommandResponse {
    fn drop(&mut self) {
        if let Some(inner) = &self.inner {
            inner.remove_response_interest(&self.id);
            inner.wake_any();
        }
    }
}

#[derive(Debug)]
struct PeerInner {
    /// The signaling channel
    signaling: fasync::Socket,

    /// A map of transaction ids that have been sent but the response has not
    /// been received and/or processed yet.
    ///
    /// Waiters are added with `add_response_waiter` and get removed when they are
    /// polled or they are removed with `remove_waiter`
    response_waiters: Mutex<Slab<ResponseWaiter>>,

    /// A queue of requests that have been received and are waiting to
    /// be reponded to, along with the waker for the task that has
    /// taken the request receiver (if it exists)
    incoming_requests: Mutex<RequestQueue>,
}

impl PeerInner {
    /// Add a response waiter, and return a id that can be used to send the
    /// transaction.  Responses then can be received using poll_recv_response
    fn add_response_waiter(&self) -> Result<TxLabel, Error> {
        let key = self
            .response_waiters
            .lock()
            .insert(ResponseWaiter::WillPoll);
        let id = TxLabel::try_from(key as u8);
        if id.is_err() {
            fx_log_warn!(tag: "avdtp", "Transaction IDs are exhausted");
            self.response_waiters.lock().remove(key);
        }
        id
    }

    /// When a waiter isn't interested in the response anymore, we need to just
    /// throw it out.  This is called when the response future is dropped.
    fn remove_response_interest(&self, id: &TxLabel) {
        let mut lock = self.response_waiters.lock();
        let idx = usize::from(id);
        if lock[idx].is_received() {
            lock.remove(idx);
        } else {
            lock[idx] = ResponseWaiter::Discard;
        }
    }

    // Attempts to receive a new request by processing all packets on the socket.
    // Resolves to an unprocessed request (header, body) if one was received.
    // Resolves to an error if there was an error reading from the socket or if the peer
    // disconnected.
    fn poll_recv_request(&self, lw: &LocalWaker) -> Poll<Result<UnparsedRequest, Error>> {
        let is_closed = self.recv_all(lw)?;

        let mut lock = self.incoming_requests.lock();

        if let Some(request) = lock.queue.pop_front() {
            Poll::Ready(Ok(request))
        } else {
            lock.listener = RequestListener::Some(lw.clone().into_waker());
            if is_closed {
                Poll::Ready(Err(Error::PeerDisconnected))
            } else {
                Poll::Pending
            }
        }
    }

    // Attempts to receive a response to a request by processing all packets on the socket.
    // Resolves to the bytes in the response body if one was received.
    // Resolves to an error if there was an error reading from the socket, if the peer
    // disconnected, or if the |label| is not being waited on.
    fn poll_recv_response(&self, label: &TxLabel, lw: &LocalWaker) -> Poll<Result<Vec<u8>, Error>> {
        let is_closed = self.recv_all(lw)?;

        let mut waiters = self.response_waiters.lock();
        let idx = usize::from(label);
        // We expect() below because the label above came from an internally-created object,
        // so the waiters should always exist in the map.
        if waiters
            .get(idx)
            .expect("Polled unregistered waiter")
            .is_received()
        {
            // We got our response.
            let buf = waiters.remove(idx).unwrap_received();
            Poll::Ready(Ok(buf))
        } else {
            // Set the waker to be notified when a response shows up.
            *waiters.get_mut(idx).expect("Polled unregistered waiter") =
                ResponseWaiter::Waiting(lw.clone().into_waker());

            if is_closed {
                Poll::Ready(Err(Error::PeerDisconnected))
            } else {
                Poll::Pending
            }
        }
    }

    /// Poll for any packets on the signaling socket
    /// Returns whether the channel was closed, or an Error::PeerRead or Error::PeerWrite
    /// if there was a problem communicating on the socket.
    fn recv_all(&self, lw: &LocalWaker) -> Result<bool, Error> {
        let mut buf = Vec::<u8>::new();
        loop {
            let packet_size = match self.signaling.poll_datagram(&mut buf, lw) {
                Poll::Ready(Err(zx::Status::PEER_CLOSED)) => {
                    fx_vlog!(tag: "avdtp", 1, "Signaling peer closed");
                    return Ok(true);
                }
                Poll::Ready(Err(e)) => return Err(Error::PeerRead(e)),
                Poll::Pending => return Ok(false),
                Poll::Ready(Ok(size)) => size,
            };
            if packet_size == 0 {
                continue;
            }
            // Detects General Reject condition and sends the response back.
            // On other headers with errors, sends BAD_HEADER to the peer
            // and attempts to continue.
            let header = match SignalingHeader::decode(buf.as_slice()) {
                Err(Error::InvalidSignalId(label, id)) => {
                    self.send_general_reject(label, id)?;
                    buf = buf.split_off(packet_size);
                    continue;
                }
                Err(_) => {
                    // Only possible other return is OutOfRange
                    // Returned only when the packet is too small, can't make a meaningful reject.
                    fx_log_info!(tag: "avdtp", "received unrejectable message");
                    buf = buf.split_off(packet_size);
                    continue;
                }
                Ok(x) => Ok(x),
            }?;
            // Commands from the remote get translated into requests.
            if header.is_command() {
                let mut lock = self.incoming_requests.lock();
                let body = buf.split_off(header.encoded_len());
                buf.clear();
                lock.queue.push_back(UnparsedRequest::new(header, body));
                if let RequestListener::Some(ref waker) = lock.listener {
                    waker.wake();
                }
            } else {
                // Should be a response to a command we sent
                let mut waiters = self.response_waiters.lock();
                let idx = usize::from(&header.label());
                let rest = buf.split_off(packet_size);
                if let Some(&ResponseWaiter::Discard) = waiters.get(idx) {
                    waiters.remove(idx);
                } else if let Some(entry) = waiters.get_mut(idx) {
                    let old_entry = mem::replace(entry, ResponseWaiter::Received(buf));
                    if let ResponseWaiter::Waiting(waker) = old_entry {
                        waker.wake();
                    }
                } else {
                    fx_vlog!(tag: "avdtp", 1, "response for {:?} we did not send, dropping", header.label());
                }
                buf = rest;
                // Note: we drop any TxLabel response we are not waiting for
            }
        }
    }

    // Wakes up an arbitrary task that has begun polling on the channel so that
    // it will call recv_all and be registered as the new channel reader.
    fn wake_any(&self) {
        // Try to wake up response waiters first, rather than the event listener.
        // The event listener is a stream, and so could be between poll_nexts,
        // Response waiters should always be actively polled once
        // they've begun being polled on a task.
        {
            let lock = self.response_waiters.lock();
            for (_, response_waiter) in lock.iter() {
                if let ResponseWaiter::Waiting(waker) = response_waiter {
                    waker.wake();
                    return;
                }
            }
        }
        {
            let lock = self.incoming_requests.lock();
            if let RequestListener::Some(waker) = &lock.listener {
                waker.wake();
                return;
            }
        }
    }

    // Build and send a General Reject message (Section 8.18)
    fn send_general_reject(&self, label: TxLabel, invalid_signal_id: u8) -> Result<(), Error> {
        // Build the packet ourselves rather than make SignalingHeader build an packet with an
        // invalid signal id.
        let packet: &[u8; 2] = &[u8::from(&label) << 4 | 0x01, invalid_signal_id & 0x3F];
        self.send_signal(packet)
    }

    fn send_response(
        &self, label: TxLabel, signal: SignalIdentifier, params: &[u8],
    ) -> Result<(), Error> {
        let header = SignalingHeader::new(label, signal, SignalingMessageType::ResponseAccept);
        let mut packet = vec![0 as u8; header.encoded_len() + params.len()];
        header.encode(packet.as_mut_slice())?;
        packet[header.encoded_len()..].clone_from_slice(params);
        self.send_signal(&packet)
    }

    fn send_reject(
        &self, label: TxLabel, signal: SignalIdentifier, error_code: ErrorCode,
    ) -> Result<(), Error> {
        self.send_reject_params(label, signal, &[u8::from(&error_code)])
    }

    fn send_reject_params(
        &self, label: TxLabel, signal: SignalIdentifier, params: &[u8],
    ) -> Result<(), Error> {
        let header = SignalingHeader::new(label, signal, SignalingMessageType::ResponseReject);
        let mut packet = vec![0 as u8; header.encoded_len() + params.len()];
        header.encode(packet.as_mut_slice())?;
        packet[header.encoded_len()..].clone_from_slice(params);
        self.send_signal(&packet)
    }

    fn send_signal(&self, data: &[u8]) -> Result<(), Error> {
        self.signaling
            .as_ref()
            .write(data)
            .map_err(|x| Error::PeerWrite(x))?;
        Ok(())
    }
}
