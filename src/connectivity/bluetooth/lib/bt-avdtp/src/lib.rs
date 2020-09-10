// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::types::Channel,
    fuchsia_zircon::{self as zx, Duration},
    futures::{
        future::FusedFuture,
        ready,
        stream::Stream,
        task::{Context, Poll, Waker},
    },
    log::{info, trace, warn},
    parking_lot::Mutex,
    slab::Slab,
    std::{collections::VecDeque, convert::TryFrom, marker::Unpin, mem, pin::Pin, sync::Arc},
};

#[cfg(test)]
mod tests;

mod rtp;
mod stream_endpoint;
mod types;

use crate::types::{
    Decodable, Encodable, SignalIdentifier, SignalingHeader, SignalingMessageType, TxLabel,
};

pub use crate::{
    rtp::{RtpError, RtpHeader},
    stream_endpoint::{MediaStream, StreamEndpoint, StreamEndpointUpdateCallback, StreamState},
    types::{
        ContentProtectionType, EndpointType, Error, ErrorCode, MediaCodecType, MediaType, Result,
        ServiceCapability, ServiceCategory, StreamEndpointId, StreamInformation,
    },
};

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
    /// Create a new peer from a signaling channel.
    pub fn new(signaling: Channel) -> Self {
        Self {
            inner: Arc::new(PeerInner {
                signaling,
                response_waiters: Mutex::new(Slab::<ResponseWaiter>::new()),
                incoming_requests: Mutex::<RequestQueue>::default(),
            }),
        }
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

        RequestStream { inner: self.inner.clone() }
    }

    /// Send a Stream End Point Discovery (Sec 8.6) command to the remote peer.
    /// Asynchronously returns a the reply in a vector of endpoint information.
    /// Error will be RemoteRejected with the error code returned by the remote
    /// if the remote peer rejected the command.
    pub async fn discover(&self) -> Result<Vec<StreamInformation>> {
        let response: Result<DiscoverResponse> =
            self.send_command(SignalIdentifier::Discover, &[]).await;
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
        &'a self,
        stream_id: &'a StreamEndpointId,
    ) -> Result<Vec<ServiceCapability>> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<GetCapabilitiesResponse> =
            self.send_command(SignalIdentifier::GetCapabilities, stream_params).await;
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
        &'a self,
        stream_id: &'a StreamEndpointId,
    ) -> Result<Vec<ServiceCapability>> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<GetCapabilitiesResponse> =
            self.send_command(SignalIdentifier::GetAllCapabilities, stream_params).await;
        match response {
            Ok(response) => Ok(response.capabilities),
            Err(e) => Err(e),
        }
    }

    /// Send a Stream Configuration (Sec 8.9) command to the remote peer for the
    /// given remote `stream_id`, communicating the association to a local
    /// `local_stream_id` and the required stream `capabilities`.
    /// Panics if `capabilities` is empty.
    /// Returns Ok(()) if the command was accepted, and RemoteConfigRejected
    /// if the remote refused.
    pub async fn set_configuration<'a>(
        &'a self,
        stream_id: &'a StreamEndpointId,
        local_stream_id: &'a StreamEndpointId,
        capabilities: &'a [ServiceCapability],
    ) -> Result<()> {
        assert!(!capabilities.is_empty(), "must set at least one capability");
        let mut params: Vec<u8> = Vec::new();
        params.resize(capabilities.iter().fold(2, |a, x| a + x.encoded_len()), 0);
        params[0] = stream_id.to_msg();
        params[1] = local_stream_id.to_msg();
        let mut idx = 2;
        for capability in capabilities {
            capability.encode(&mut params[idx..])?;
            idx += capability.encoded_len();
        }
        let response: Result<SimpleResponse> =
            self.send_command(SignalIdentifier::SetConfiguration, &params).await;
        response.and(Ok(()))
    }

    /// Send a Get Stream Configuration (Sec 8.10) command to the remote peer
    /// for the given remote `stream_id`.
    /// Asynchronously returns the set of ServiceCapabilities previously
    /// configured between these two peers.
    /// Error will be RemoteRejected with the error code reported by the remote
    /// if the remote peer rejects this command.
    pub async fn get_configuration<'a>(
        &'a self,
        stream_id: &'a StreamEndpointId,
    ) -> Result<Vec<ServiceCapability>> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<GetCapabilitiesResponse> =
            self.send_command(SignalIdentifier::GetConfiguration, stream_params).await;
        match response {
            Ok(response) => Ok(response.capabilities),
            Err(e) => Err(e),
        }
    }

    /// Send a Stream Reconfigure (Sec 8.11) command to the remote peer for the
    /// given remote `stream_id`, to reconfigure the Application Service
    /// capabilities in `capabilities`.
    /// Note: Per the spec, only the Media Codec and Content Protection
    /// capabilities will be accepted in this command.
    /// Panics if there are no capabilities to configure.
    /// Returns Ok(()) if the command was accepted, and RemoteConfigRejected
    /// if the remote refused.
    pub async fn reconfigure<'a>(
        &'a self,
        stream_id: &'a StreamEndpointId,
        capabilities: &'a [ServiceCapability],
    ) -> Result<()> {
        assert!(!capabilities.is_empty(), "must set at least one capability");
        let mut params: Vec<u8> = Vec::new();
        params.resize(capabilities.iter().fold(1, |a, x| a + x.encoded_len()), 0);
        params[0] = stream_id.to_msg();
        let mut idx = 1;
        for capability in capabilities {
            if !capability.is_application() {
                return Err(Error::Encoding);
            }
            capability.encode(&mut params[idx..])?;
            idx += capability.encoded_len();
        }
        let response: Result<SimpleResponse> =
            self.send_command(SignalIdentifier::Reconfigure, &params).await;
        response.and(Ok(()))
    }

    /// Send a Open Stream Command (Sec 8.12) to the remote peer for the given
    /// `stream_id`.
    /// Returns Ok(()) if the command is accepted, and RemoteRejected if the
    /// remote peer rejects the command with the code returned by the remote.
    pub async fn open<'a>(&'a self, stream_id: &'a StreamEndpointId) -> Result<()> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<SimpleResponse> =
            self.send_command(SignalIdentifier::Open, stream_params).await;
        response.and(Ok(()))
    }

    /// Send a Start Stream Command (Sec 8.13) to the remote peer for all the
    /// streams in `stream_ids`.
    /// Returns Ok(()) if the command is accepted, and RemoteStreamRejected
    /// with the stream endpoint id and error code reported by the remote if
    /// the remote signals a failure.
    pub async fn start<'a>(&'a self, stream_ids: &'a [StreamEndpointId]) -> Result<()> {
        let mut stream_params = Vec::with_capacity(stream_ids.len());
        for stream_id in stream_ids {
            stream_params.push(stream_id.to_msg());
        }
        let response: Result<SimpleResponse> =
            self.send_command(SignalIdentifier::Start, &stream_params).await;
        response.and(Ok(()))
    }

    /// Send a Close Stream Command (Sec 8.14) to the remote peer for the given
    /// `stream_id`.
    /// Returns Ok(()) if the command is accepted, and RemoteRejected if the
    /// remote peer rejects the command with the code returned by the remote.
    pub async fn close<'a>(&'a self, stream_id: &'a StreamEndpointId) -> Result<()> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<SimpleResponse> =
            self.send_command(SignalIdentifier::Close, stream_params).await;
        response.and(Ok(()))
    }

    /// Send a Suspend Command (Sec 8.15) to the remote peer for all the
    /// streams in `stream_ids`.
    /// Returns Ok(()) if the command is accepted, and RemoteStreamRejected
    /// with the stream endpoint id and error code reported by the remote if
    /// the remote signals a failure.
    pub async fn suspend<'a>(&'a self, stream_ids: &'a [StreamEndpointId]) -> Result<()> {
        let mut stream_params = Vec::with_capacity(stream_ids.len());
        for stream_id in stream_ids {
            stream_params.push(stream_id.to_msg());
        }
        let response: Result<SimpleResponse> =
            self.send_command(SignalIdentifier::Suspend, &stream_params).await;
        response.and(Ok(()))
    }

    /// Send an Abort (Sec 8.16) to the remote peer for the given `stream_id`.
    /// Returns Ok(()) if the command is accepted, and Err(Timeout) if the remote
    /// timed out.  The remote peer is not allowed to reject this command, and
    /// commands that have invalid `stream_id` will timeout instead.
    pub async fn abort<'a>(&'a self, stream_id: &'a StreamEndpointId) -> Result<()> {
        let stream_params = &[stream_id.to_msg()];
        let response: Result<SimpleResponse> =
            self.send_command(SignalIdentifier::Abort, stream_params).await;
        response.and(Ok(()))
    }

    /// Send a Delay Report (Sec 8.19) to the remote peer for the given `stream_id`.
    /// `delay` is in tenths of milliseconds.
    /// Returns Ok(()) if the command is accepted, and RemoteRejected if the
    /// remote peer rejects the command with the code returned by the remote.
    pub async fn delay_report<'a>(
        &'a self,
        stream_id: &'a StreamEndpointId,
        delay: u16,
    ) -> Result<()> {
        let delay_bytes: [u8; 2] = delay.to_be_bytes();
        let params = &[stream_id.to_msg(), delay_bytes[0], delay_bytes[1]];
        let response: Result<SimpleResponse> =
            self.send_command(SignalIdentifier::DelayReport, params).await;
        response.and(Ok(()))
    }

    /// The maximum amount of time we will wait for a response to a signaling command.
    const RTX_SIG_TIMER_MS: i64 = 3000;
    const COMMAND_TIMEOUT: Duration = Duration::from_millis(Peer::RTX_SIG_TIMER_MS);

    /// Sends a signal on the channel and receive a future that will complete
    /// when we get the expected response.
    async fn send_command<'a, D: Decodable>(
        &'a self,
        signal: SignalIdentifier,
        payload: &'a [u8],
    ) -> Result<D> {
        let id = self.inner.add_response_waiter()?;
        let header = SignalingHeader::new(id, signal, SignalingMessageType::Command);

        {
            let mut buf = vec![0; header.encoded_len()];

            header.encode(buf.as_mut_slice())?;
            buf.extend_from_slice(payload);

            self.inner.send_signal(buf.as_slice())?;
        }

        let response = CommandResponse { id: header.label(), inner: Some(self.inner.clone()) };

        let deadline = Peer::COMMAND_TIMEOUT.after_now();
        let response_buf = response.on_timeout(deadline, || Err(Error::Timeout)).await?;
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
    SetConfiguration {
        local_stream_id: StreamEndpointId,
        remote_stream_id: StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
        responder: ConfigureResponder,
    },
    GetConfiguration {
        stream_id: StreamEndpointId,
        responder: GetCapabilitiesResponder,
    },
    Reconfigure {
        local_stream_id: StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
        responder: ConfigureResponder,
    },
    Open {
        stream_id: StreamEndpointId,
        responder: SimpleResponder,
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
    DelayReport {
        stream_id: StreamEndpointId,
        delay: u16,
        responder: SimpleResponder,
    }, // TODO(jamuraa): add the rest of the requests
}

macro_rules! parse_one_seid {
    ($body:ident, $signal:ident, $peer:ident, $id:ident, $request_variant:ident, $responder_type:ident) => {
        if $body.len() != 1 {
            Err(Error::RequestInvalid(ErrorCode::BadLength))
        } else {
            Ok(Request::$request_variant {
                stream_id: StreamEndpointId::from_msg(&$body[0]),
                responder: $responder_type { signal: $signal, peer: $peer, id: $id },
            })
        }
    };
}

impl Request {
    fn get_req_seids(body: &[u8]) -> Result<Vec<StreamEndpointId>> {
        if body.len() < 1 {
            return Err(Error::RequestInvalid(ErrorCode::BadLength));
        }
        Ok(body.iter().map(&StreamEndpointId::from_msg).collect())
    }

    fn get_req_capabilities(encoded: &[u8]) -> Result<Vec<ServiceCapability>> {
        if encoded.len() < 2 {
            return Err(Error::RequestInvalid(ErrorCode::BadLength));
        }
        let mut caps = vec![];
        let mut loc = 0;
        while loc < encoded.len() {
            let cap = match ServiceCapability::decode(&encoded[loc..]) {
                Ok(cap) => cap,
                Err(Error::RequestInvalid(code)) => {
                    return Err(Error::RequestInvalidExtra(code, encoded[loc]));
                }
                Err(e) => return Err(e),
            };
            loc += cap.encoded_len();
            caps.push(cap);
        }
        Ok(caps)
    }

    fn parse(
        peer: Arc<PeerInner>,
        id: TxLabel,
        signal: SignalIdentifier,
        body: &[u8],
    ) -> Result<Request> {
        match signal {
            SignalIdentifier::Discover => {
                // Discover Request has no body (Sec 8.6.1)
                if body.len() > 0 {
                    return Err(Error::RequestInvalid(ErrorCode::BadLength));
                }
                Ok(Request::Discover { responder: DiscoverResponder { peer, id } })
            }
            SignalIdentifier::GetCapabilities => {
                parse_one_seid!(body, signal, peer, id, GetCapabilities, GetCapabilitiesResponder)
            }
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
            SignalIdentifier::GetConfiguration => {
                parse_one_seid!(body, signal, peer, id, GetConfiguration, GetCapabilitiesResponder)
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
                            (&x.category()).into(),
                        ));
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
            SignalIdentifier::DelayReport => {
                if body.len() != 3 {
                    return Err(Error::RequestInvalid(ErrorCode::BadLength));
                }
                let delay_arr: [u8; 2] = [body[1], body[2]];
                let delay = u16::from_be_bytes(delay_arr);
                Ok(Request::DelayReport {
                    stream_id: StreamEndpointId::from_msg(&body[0]),
                    delay,
                    responder: SimpleResponder { signal, peer, id },
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
    type Item = Result<Request>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        Poll::Ready(match ready!(self.inner.poll_recv_request(cx)) {
            Ok(UnparsedRequest(SignalingHeader { label, signal, .. }, body)) => {
                match Request::parse(self.inner.clone(), label, signal, &body) {
                    Err(Error::RequestInvalid(code)) => {
                        self.inner.send_reject(label, signal, code)?;
                        return Poll::Pending;
                    }
                    Err(Error::RequestInvalidExtra(code, extra)) => {
                        self.inner.send_reject_params(label, signal, &[extra, u8::from(&code)])?;
                        return Poll::Pending;
                    }
                    Err(Error::UnimplementedMessage) => {
                        self.inner.send_reject(label, signal, ErrorCode::NotSupportedCommand)?;
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

// Simple responses have no body data.
#[derive(Debug)]
pub struct SimpleResponse {}

impl Decodable for SimpleResponse {
    fn decode(from: &[u8]) -> Result<Self> {
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
    fn decode(from: &[u8]) -> Result<Self> {
        let mut endpoints = Vec::<StreamInformation>::new();
        let mut idx = 0;
        while idx < from.len() {
            let endpoint = StreamInformation::decode(&from[idx..])?;
            idx += endpoint.encoded_len();
            endpoints.push(endpoint);
        }
        Ok(DiscoverResponse { endpoints })
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
    pub fn send(self, endpoints: &[StreamInformation]) -> Result<()> {
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
        self.peer.send_response(self.id, SignalIdentifier::Discover, &params)
    }

    pub fn reject(self, error_code: ErrorCode) -> Result<()> {
        self.peer.send_reject(self.id, SignalIdentifier::Discover, error_code)
    }
}

#[derive(Debug)]
pub struct GetCapabilitiesResponder {
    peer: Arc<PeerInner>,
    signal: SignalIdentifier,
    id: TxLabel,
}

impl GetCapabilitiesResponder {
    pub fn send(self, capabilities: &[ServiceCapability]) -> Result<()> {
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

    pub fn reject(self, error_code: ErrorCode) -> Result<()> {
        self.peer.send_reject(self.id, self.signal, error_code)
    }
}

#[derive(Debug)]
struct GetCapabilitiesResponse {
    capabilities: Vec<ServiceCapability>,
}

impl Decodable for GetCapabilitiesResponse {
    fn decode(from: &[u8]) -> Result<Self> {
        let mut capabilities = Vec::<ServiceCapability>::new();
        let mut idx = 0;
        while idx < from.len() {
            match ServiceCapability::decode(&from[idx..]) {
                Ok(capability) => {
                    idx = idx + capability.encoded_len();
                    capabilities.push(capability);
                }
                Err(_) => {
                    // The capability length of the invalid capability can be nonzero.
                    // Advance `idx` by the payload amount, but don't push the invalid capability.
                    // Increment by 1 byte for ServiceCategory, 1 byte for payload length,
                    // `length_of_capability` bytes for capability length.
                    info!(
                        "GetCapabilitiesResponse decode: Capability {:?} not supported.",
                        from[idx]
                    );
                    let length_of_capability = from[idx + 1] as usize;
                    idx = idx + 2 + length_of_capability;
                }
            }
        }
        Ok(GetCapabilitiesResponse { capabilities })
    }
}

#[derive(Debug)]
pub struct SimpleResponder {
    peer: Arc<PeerInner>,
    signal: SignalIdentifier,
    id: TxLabel,
}

impl SimpleResponder {
    pub fn send(self) -> Result<()> {
        self.peer.send_response(self.id, self.signal, &[])
    }

    pub fn reject(self, error_code: ErrorCode) -> Result<()> {
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
    pub fn send(self) -> Result<()> {
        self.peer.send_response(self.id, self.signal, &[])
    }

    pub fn reject(self, stream_id: &StreamEndpointId, error_code: ErrorCode) -> Result<()> {
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
    pub fn send(self) -> Result<()> {
        self.peer.send_response(self.id, self.signal, &[])
    }

    pub fn reject(self, category: ServiceCategory, error_code: ErrorCode) -> Result<()> {
        self.peer.send_reject_params(
            self.id,
            self.signal,
            &[u8::from(&category), u8::from(&error_code)],
        )
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
    /// Someone is listening, and can be woken with the waker.
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
    /// It's still waiting on the response, but the receiver has decided they
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
    expected_signal: SignalIdentifier,
    buf: Vec<u8>,
) -> Result<D> {
    let header = SignalingHeader::decode(buf.as_slice())?;
    if header.signal() != expected_signal {
        return Err(Error::InvalidHeader);
    }
    if !header.is_type(SignalingMessageType::ResponseAccept) {
        let params_idx = header.encoded_len();
        match header.signal() {
            SignalIdentifier::Start | SignalIdentifier::Suspend => {
                return Err(Error::RemoteStreamRejected(buf[params_idx] >> 2, buf[params_idx + 1]));
            }
            SignalIdentifier::SetConfiguration | SignalIdentifier::Reconfigure => {
                return Err(Error::RemoteConfigRejected(buf[params_idx], buf[params_idx + 1]));
            }
            _ => return Err(Error::RemoteRejected(buf[params_idx])),
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
    type Output = Result<Vec<u8>>;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        let res;
        {
            let client = this.inner.as_ref().ok_or(Error::AlreadyReceived)?;
            res = client.poll_recv_response(&this.id, cx);
        }

        if let Poll::Ready(Ok(_)) = res {
            let inner = this.inner.take().expect("CommandResponse polled after completion");
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
    signaling: Channel,

    /// A map of transaction ids that have been sent but the response has not
    /// been received and/or processed yet.
    ///
    /// Waiters are added with `add_response_waiter` and get removed when they are
    /// polled or they are removed with `remove_waiter`
    response_waiters: Mutex<Slab<ResponseWaiter>>,

    /// A queue of requests that have been received and are waiting to
    /// be responded to, along with the waker for the task that has
    /// taken the request receiver (if it exists)
    incoming_requests: Mutex<RequestQueue>,
}

impl PeerInner {
    /// Add a response waiter, and return a id that can be used to send the
    /// transaction.  Responses then can be received using poll_recv_response
    fn add_response_waiter(&self) -> Result<TxLabel> {
        let key = self.response_waiters.lock().insert(ResponseWaiter::WillPoll);
        let id = TxLabel::try_from(key as u8);
        if id.is_err() {
            warn!("Transaction IDs are exhausted");
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
    fn poll_recv_request(&self, cx: &mut Context<'_>) -> Poll<Result<UnparsedRequest>> {
        let is_closed = self.recv_all(cx)?;

        let mut lock = self.incoming_requests.lock();

        if let Some(request) = lock.queue.pop_front() {
            Poll::Ready(Ok(request))
        } else {
            lock.listener = RequestListener::Some(cx.waker().clone());
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
    fn poll_recv_response(&self, label: &TxLabel, cx: &mut Context<'_>) -> Poll<Result<Vec<u8>>> {
        let is_closed = self.recv_all(cx)?;

        let mut waiters = self.response_waiters.lock();
        let idx = usize::from(label);
        // We expect() below because the label above came from an internally-created object,
        // so the waiters should always exist in the map.
        if waiters.get(idx).expect("Polled unregistered waiter").is_received() {
            // We got our response.
            let buf = waiters.remove(idx).unwrap_received();
            Poll::Ready(Ok(buf))
        } else {
            // Set the waker to be notified when a response shows up.
            *waiters.get_mut(idx).expect("Polled unregistered waiter") =
                ResponseWaiter::Waiting(cx.waker().clone());

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
    fn recv_all(&self, cx: &mut Context<'_>) -> Result<bool> {
        let mut buf = Vec::<u8>::new();
        loop {
            let packet_size = match self.signaling.poll_datagram(cx, &mut buf) {
                Poll::Ready(Err(zx::Status::PEER_CLOSED)) => {
                    trace!("Signaling peer closed");
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
                    info!("received unrejectable message");
                    buf = buf.split_off(packet_size);
                    continue;
                }
                Ok(x) => x,
            };
            // Commands from the remote get translated into requests.
            if header.is_command() {
                let mut lock = self.incoming_requests.lock();
                let body = buf.split_off(header.encoded_len());
                buf.clear();
                lock.queue.push_back(UnparsedRequest::new(header, body));
                if let RequestListener::Some(ref waker) = lock.listener {
                    waker.wake_by_ref();
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
                    trace!("response for {:?} we did not send, dropping", header.label());
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
                    waker.wake_by_ref();
                    return;
                }
            }
        }
        {
            let lock = self.incoming_requests.lock();
            if let RequestListener::Some(waker) = &lock.listener {
                waker.wake_by_ref();
                return;
            }
        }
    }

    // Build and send a General Reject message (Section 8.18)
    fn send_general_reject(&self, label: TxLabel, invalid_signal_id: u8) -> Result<()> {
        // Build the packet ourselves rather than make SignalingHeader build an packet with an
        // invalid signal id.
        let packet: &[u8; 2] = &[u8::from(&label) << 4 | 0x01, invalid_signal_id & 0x3F];
        self.send_signal(packet)
    }

    fn send_response(&self, label: TxLabel, signal: SignalIdentifier, params: &[u8]) -> Result<()> {
        let header = SignalingHeader::new(label, signal, SignalingMessageType::ResponseAccept);
        let mut packet = vec![0 as u8; header.encoded_len() + params.len()];
        header.encode(packet.as_mut_slice())?;
        packet[header.encoded_len()..].clone_from_slice(params);
        self.send_signal(&packet)
    }

    fn send_reject(
        &self,
        label: TxLabel,
        signal: SignalIdentifier,
        error_code: ErrorCode,
    ) -> Result<()> {
        self.send_reject_params(label, signal, &[u8::from(&error_code)])
    }

    fn send_reject_params(
        &self,
        label: TxLabel,
        signal: SignalIdentifier,
        params: &[u8],
    ) -> Result<()> {
        let header = SignalingHeader::new(label, signal, SignalingMessageType::ResponseReject);
        let mut packet = vec![0 as u8; header.encoded_len() + params.len()];
        header.encode(packet.as_mut_slice())?;
        packet[header.encoded_len()..].clone_from_slice(params);
        self.send_signal(&packet)
    }

    fn send_signal(&self, data: &[u8]) -> Result<()> {
        self.signaling.as_ref().write(data).map_err(|x| Error::PeerWrite(x))?;
        Ok(())
    }
}
