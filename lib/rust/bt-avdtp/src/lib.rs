// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(arbitrary_self_types, async_await, await_macro, futures_api, pin)]

#[macro_use]
extern crate failure;

use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log, fx_log_info, fx_log_warn, fx_vlog};
use fuchsia_zircon as zx;
use futures::ready;
use futures::stream::Stream;
use futures::task::{LocalWaker, Poll, Waker};
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

    /// Send a "Discover" command to the remote peer.  Asynchronously returns
    /// a vector of discovered endpoint information.
    /// Error will be RemoteRejected with the error code returned by the remote peer
    /// if the remote peer rejected the command, or an appropriate other error.
    pub async fn discover(&self) -> Result<Vec<StreamInformation>, Error> {
        let response: Result<DiscoverResponse, Error> =
            await!(self.send_command(SignalIdentifier::Discover, &[]));
        match response {
            Ok(response) => Ok(response.endpoints),
            Err(e) => Err(e),
        }
    }

    pub async fn get_capabilities<'a>(
        &'a self, stream_id: &'a StreamEndpointId,
    ) -> Result<Vec<ServiceCapability>, Error> {
        let stream_params = &[stream_id.0 << 2];
        let response: Result<GetCapabilitiesResponse, Error> =
            await!(self.send_command(SignalIdentifier::GetCapabilities, stream_params));
        match response {
            Ok(response) => Ok(response.capabilities),
            Err(e) => Err(e),
        }
    }

    pub async fn get_all_capabilities<'a>(
        &'a self, stream_id: &'a StreamEndpointId,
    ) -> Result<Vec<ServiceCapability>, Error> {
        let stream_params = &[stream_id.0 << 2];
        let response: Result<GetCapabilitiesResponse, Error> =
            await!(self.send_command(SignalIdentifier::GetAllCapabilities, stream_params));
        match response {
            Ok(response) => Ok(response.capabilities),
            Err(e) => Err(e),
        }
    }

    /// Send a signal on the socket and receive a future that will complete
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

        let response_buf = await!(CommandResponse {
            id: header.label(),
            inner: Some(self.inner.clone()),
        })?;

        decode_signaling_response(header.signal(), response_buf)
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
    // TODO(jamuraa): add the rest of the requests
}

impl Request {
    fn parse(
        peer: Arc<PeerInner>, id: TxLabel, signal: SignalIdentifier, body: &[u8],
    ) -> Result<Request, Error> {
        match signal {
            SignalIdentifier::Discover => {
                // Discover Request has no body (Sec 8.6.1)
                if body.len() > 0 {
                    return Err(Error::BadLength);
                }
                Ok(Request::Discover {
                    responder: DiscoverResponder { peer: peer, id: id },
                })
            }
            SignalIdentifier::GetCapabilities => {
                if body.len() != 1 {
                    return Err(Error::BadLength);
                }
                Ok(Request::GetCapabilities {
                    stream_id: StreamEndpointId(body[0] >> 2),
                    responder: GetCapabilitiesResponder {
                        signal: signal,
                        peer: peer,
                        id: id,
                    },
                })
            }
            SignalIdentifier::GetAllCapabilities => {
                if body.len() != 1 {
                    return Err(Error::BadLength);
                }
                Ok(Request::GetAllCapabilities {
                    stream_id: StreamEndpointId(body[0] >> 2),
                    responder: GetCapabilitiesResponder {
                        signal: signal,
                        peer: peer,
                        id: id,
                    },
                })
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
                    Err(Error::BadLength) => {
                        self.inner
                            .send_reject(label, signal, ErrorCode::BadLength)?;
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
        let id = StreamEndpointId(from[0] >> 2);
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
        into[0] = self.id.0 << 2 | if self.in_use { 0x02 } else { 0x00 };
        into[1] = u8::from(&self.media_type) << 4 | u8::from(&self.endpoint_type) << 3;
        Ok(())
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
    /// Will result in a Error::PeerWrite if the distant peer is disconnected.
    pub fn send(self, endpoints: &[StreamInformation]) -> Result<(), Error> {
        let header = SignalingHeader::new(
            self.id,
            SignalIdentifier::Discover,
            SignalingMessageType::ResponseAccept,
        );
        let mut reply = vec![0 as u8; header.encoded_len() + endpoints.len() * 2];
        header.encode(&mut reply[0..header.encoded_len()])?;
        let mut idx = header.encoded_len();
        for endpoint in endpoints {
            endpoint.encode(&mut reply[idx..idx + endpoint.encoded_len()])?;
            idx += endpoint.encoded_len();
        }
        self.peer.send_signal(reply.as_slice())
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
        let header =
            SignalingHeader::new(self.id, self.signal, SignalingMessageType::ResponseAccept);
        let included_iter = capabilities.iter().filter(|x| x.in_response(self.signal));
        let reply_len = included_iter.clone().fold(header.encoded_len(), |a, b| a + b.encoded_len());
        let mut reply = vec![0 as u8; reply_len];
        header.encode(&mut reply[0..header.encoded_len()])?;
        let mut pos = header.encoded_len();
        for capability in included_iter {
            let size = capability.encoded_len();
            capability.encode(&mut reply[pos..pos + size])?;
            pos += size;
        }
        self.peer.send_signal(reply.as_slice())
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
}

impl Decodable for ServiceCapability {
    fn decode(from: &[u8]) -> Result<Self, Error> {
        if from.len() < 2 {
            return Err(Error::Encoding);
        }
        let length_of_capability = from[1] as usize;
        let d = match from[0] {
            1 => ServiceCapability::MediaTransport,
            2 => ServiceCapability::Reporting,
            3 => {
                if from.len() < 5 {
                    return Err(Error::Encoding);
                }
                ServiceCapability::Recovery {
                    recovery_type: from[2],
                    max_recovery_window_size: from[3],
                    max_number_media_packets: from[4],
                }
            }
            4 => {
                let prot =
                    ContentProtectionType::try_from(((from[3] as u16) << 8) + from[2] as u16)?;
                let extra_len = length_of_capability - 2;
                let mut extra = vec![0; extra_len];
                extra.copy_from_slice(&from[4..4 + extra_len]);
                ServiceCapability::ContentProtection {
                    protection_type: prot,
                    extra: extra,
                }
            }
            7 => {
                let media = MediaType::try_from(from[2] >> 4)?;
                let codec_type = MediaCodecType::new(from[3]);
                let extra_len = length_of_capability - 2;
                let mut codec_extra = vec![0; extra_len];
                codec_extra.copy_from_slice(&from[4..4 + extra_len]);
                ServiceCapability::MediaCodec {
                    media_type: media,
                    codec_type: codec_type,
                    codec_extra: codec_extra,
                }
            }
            8 => ServiceCapability::DelayReporting,
            _ => {
                return Err(Error::Encoding);
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
        return Err(Error::RemoteRejected(buf[header.encoded_len()]));
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

    fn send_reject(
        &self, label: TxLabel, signal: SignalIdentifier, error_code: ErrorCode,
    ) -> Result<(), Error> {
        let header = SignalingHeader::new(label, signal, SignalingMessageType::ResponseReject);
        let mut packet = vec![0 as u8; header.encoded_len() + 1];
        header.encode(packet.as_mut_slice())?;
        packet[header.encoded_len()] = u8::from(&error_code);
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
