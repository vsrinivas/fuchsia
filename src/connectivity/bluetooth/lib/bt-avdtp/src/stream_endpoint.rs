// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::types::Channel,
    fuchsia_zircon::{Duration, DurationNum, Status},
    futures::{io, stream::Stream},
    parking_lot::Mutex,
    std::{
        convert::TryFrom,
        fmt,
        pin::Pin,
        sync::Arc,
        sync::RwLock,
        sync::Weak,
        task::{Context, Poll},
    },
};

use crate::{
    types::{
        EndpointType, Error, ErrorCode, MediaCodecType, MediaType, Result as AvdtpResult,
        ServiceCapability, ServiceCategory, StreamEndpointId, StreamInformation,
    },
    Peer, SimpleResponder,
};

pub type StreamEndpointUpdateCallback = Box<dyn Fn(&StreamEndpoint) -> () + Sync + Send>;

#[derive(PartialEq, Debug)]
pub enum StreamState {
    Idle,
    Configured,
    // An Open command has been accepted, but streams have not been established yet.
    Opening,
    Open,
    Streaming,
    Closing,
    Aborting,
}

impl StreamState {
    fn configured(&self) -> bool {
        match self {
            StreamState::Configured
            | StreamState::Opening
            | StreamState::Open
            | StreamState::Streaming => true,
            _ => false,
        }
    }
}

/// An AVDTP Transport Stream, which implements the Basic service
/// See Section 7.2
/// Audio frames are currently not delivered anywhere, and are counted and dropped.
/// TODO(jamuraa): setup a delivery mechanism that is compatible with Media
pub struct StreamEndpoint {
    /// Local stream endpoint id.  This should be unique per AVDTP Peer.
    id: StreamEndpointId,
    /// The type of endpoint this is (TSEP), Source or Sink.
    endpoint_type: EndpointType,
    /// The media type this stream represents.
    media_type: MediaType,
    /// Current state the stream is in. See Section 6.5 for an overview.
    state: StreamState,
    /// The media transport channel
    /// This should be Some(channel) when state is Open or Streaming.
    transport: Option<Arc<RwLock<Channel>>>,
    /// True when the MediaStream is held.
    /// Prevents multiple threads from owning the media stream.
    stream_held: Arc<Mutex<bool>>,
    /// The capabilities of this endpoint.
    capabilities: Vec<ServiceCapability>,
    /// The remote stream endpoint id.  None if the stream has never been configured.
    remote_id: Option<StreamEndpointId>,
    /// The current configuration of this endpoint.  Empty if the stream has never been configured.
    configuration: Vec<ServiceCapability>,
    /// Callback that is run whenever the endpoint is updated
    update_callback: Option<StreamEndpointUpdateCallback>,
}

impl fmt::Debug for StreamEndpoint {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("StreamEndpoint")
            .field("id", &self.id.to_string())
            .field("endpoint_type", &self.endpoint_type)
            .field("media_type", &self.media_type)
            .field("state", &self.state)
            .field("capabilities", &self.capabilities)
            .field("remote_id", &self.remote_id.as_ref().map(|id| id.to_string()))
            .field("configuration", &self.configuration)
            .finish()
    }
}

impl StreamEndpoint {
    /// Make a new StreamEndpoint.
    /// |id| must be in the valid range for a StreamEndpointId (0x01 - 0x3E).
    /// StreamEndpoints start in the Idle state.
    pub fn new(
        id: u8,
        media_type: MediaType,
        endpoint_type: EndpointType,
        capabilities: Vec<ServiceCapability>,
    ) -> AvdtpResult<StreamEndpoint> {
        let seid = StreamEndpointId::try_from(id)?;
        Ok(StreamEndpoint {
            id: seid,
            capabilities,
            media_type,
            endpoint_type,
            state: StreamState::Idle,
            transport: None,
            stream_held: Arc::new(Mutex::new(false)),
            remote_id: None,
            configuration: vec![],
            update_callback: None,
        })
    }

    pub fn as_new(&self) -> Self {
        StreamEndpoint::new(
            self.id.0,
            self.media_type.clone(),
            self.endpoint_type.clone(),
            self.capabilities.clone(),
        )
        .expect("as_new")
    }

    /// Set the state to the given value and run the `update_callback` afterwards
    fn set_state(&mut self, state: StreamState) {
        self.state = state;
        self.update_callback();
    }

    /// Pass update callback to StreamEndpoint that will be called anytime `StreamEndpoint` is
    /// modified.
    pub fn set_update_callback(&mut self, callback: Option<StreamEndpointUpdateCallback>) {
        self.update_callback = callback;
    }

    fn update_callback(&self) {
        self.update_callback.as_ref().map(|cb| cb(self));
    }

    /// Build a new StreamEndpoint from a StreamInformation and associated Capabilities.
    /// This makes it easy to build from AVDTP Discover and GetCapabilities procedures.
    /// StreamEndpooints start in the Idle state.
    pub fn from_info(
        info: &StreamInformation,
        capabilities: Vec<ServiceCapability>,
    ) -> StreamEndpoint {
        StreamEndpoint {
            id: info.id().clone(),
            capabilities: capabilities,
            media_type: info.media_type().clone(),
            endpoint_type: info.endpoint_type().clone(),
            state: StreamState::Idle,
            transport: None,
            stream_held: Arc::new(Mutex::new(false)),
            remote_id: None,
            configuration: vec![],
            update_callback: None,
        }
    }

    /// Attempt to Configure this stream using the capabilities given.
    /// If the stream is not in an Idle state, fails with Err(InvalidState).
    /// Used for the Stream Configuration procedure, see Section 6.9
    pub fn configure(
        &mut self,
        remote_id: &StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
    ) -> Result<(), (ServiceCategory, ErrorCode)> {
        if self.state != StreamState::Idle {
            return Err((ServiceCategory::None, ErrorCode::BadState));
        }
        self.remote_id = Some(remote_id.clone());
        for cap in &capabilities {
            if !self
                .capabilities
                .iter()
                .any(|y| std::mem::discriminant(cap) == std::mem::discriminant(y))
            {
                return Err((cap.category(), ErrorCode::UnsupportedConfiguration));
            }
        }
        self.configuration = capabilities;
        self.set_state(StreamState::Configured);
        Ok(())
    }

    /// Attempt to reconfigure this stream with the capabilities given.  If any capability is not
    /// valid to set, fails with the first such category and InvalidCapabilities If the stream is
    /// not in the Open state, fails with Err((None, BadState)) Used for the Stream Reconfiguration
    /// procedure, see Section 6.15.
    pub fn reconfigure(
        &mut self,
        mut capabilities: Vec<ServiceCapability>,
    ) -> Result<(), (ServiceCategory, ErrorCode)> {
        if self.state != StreamState::Open {
            return Err((ServiceCategory::None, ErrorCode::BadState));
        }
        // Only application capabilities are allowed to be reconfigured. See Section 8.11.1
        if let Some(cap) = capabilities.iter().find(|x| !x.is_application()) {
            return Err((cap.category(), ErrorCode::InvalidCapabilities));
        }
        // Should only replace the capabilities that have been configured. See Section 8.11.2
        let to_replace: std::vec::Vec<_> =
            capabilities.iter().map(|x| std::mem::discriminant(x)).collect();
        self.configuration.retain(|x| {
            let disc = std::mem::discriminant(x);
            !to_replace.contains(&disc)
        });
        self.configuration.append(&mut capabilities);
        self.update_callback();
        Ok(())
    }

    /// Get the current configuration of this stream.
    /// If the stream is not configured, returns None.
    /// Used for the Steam Get Configuration Procedure, see Section 6.10
    pub fn get_configuration(&self) -> Option<&Vec<ServiceCapability>> {
        if !self.state.configured() {
            return None;
        }
        Some(&self.configuration)
    }

    /// When a L2CAP channel is received after an Open command is accepted, it should be
    /// delivered via receive_channel.
    /// Returns true if this Endpoint expects more channels to be established before
    /// streaming is started.
    /// Returns Err(InvalidState) if this Endpoint is not expecting a channel to be established,
    /// closing |c|.
    pub fn receive_channel(&mut self, c: Channel) -> AvdtpResult<bool> {
        if self.state != StreamState::Opening || self.transport.is_some() {
            return Err(Error::InvalidState);
        }
        self.transport = Some(Arc::new(RwLock::new(c)));
        self.stream_held = Arc::new(Mutex::new(false));
        // TODO(jamuraa, NET-1674, NET-1675): Reporting and Recovery channels
        self.set_state(StreamState::Open);
        Ok(false)
    }

    /// Begin opening this stream.  The stream must be in a Configured state.
    /// See Stream Establishment, Section 6.11
    pub fn establish(&mut self) -> Result<(), ErrorCode> {
        if self.state != StreamState::Configured || self.transport.is_some() {
            return Err(ErrorCode::BadState);
        }
        self.set_state(StreamState::Opening);
        Ok(())
    }

    pub async fn wait_for_channel_close(&self, timeout: Duration) -> Result<(), Status> {
        if self.transport.is_none() {
            return Ok(());
        }
        let channel =
            self.transport.as_ref().unwrap().try_read().map_err(|_e| Status::BAD_STATE)?;
        let closed_fut = channel.closed();
        closed_fut.on_timeout(timeout.after_now(), || Err(Status::TIMED_OUT)).await
    }

    /// Close this stream.  This procedure checks that the media channels are closed.
    /// If the channels are not closed in 3 seconds, it initiates an abort procedure with the
    /// remote |peer| and completes when that finishes.
    pub async fn release<'a>(
        &'a mut self,
        responder: SimpleResponder,
        peer: &'a Peer,
    ) -> AvdtpResult<()> {
        if self.state != StreamState::Open && self.state != StreamState::Streaming {
            return responder.reject(ErrorCode::BadState);
        }
        self.set_state(StreamState::Closing);
        responder.send()?;
        let timeout = 3.seconds();
        if let Err(Status::TIMED_OUT) = self.wait_for_channel_close(timeout).await {
            return Ok(self.abort(Some(peer)).await);
        }
        // Closing returns this endpoint to the Idle state.
        self.configuration.clear();
        self.remote_id = None;
        self.set_state(StreamState::Idle);
        Ok(())
    }

    /// Start this stream.  This can be done only from the Open State.
    /// Used for the Stream Start procedure, See Section 6.12
    pub fn start(&mut self) -> Result<(), ErrorCode> {
        if self.state != StreamState::Open {
            return Err(ErrorCode::BadState);
        }
        self.set_state(StreamState::Streaming);
        Ok(())
    }

    /// Suspend this stream.  This can be done only from the Streaming state.
    /// Used for the Stream Suspend procedure, See Section 6.14
    pub fn suspend(&mut self) -> Result<(), ErrorCode> {
        if self.state != StreamState::Streaming {
            return Err(ErrorCode::BadState);
        }
        self.set_state(StreamState::Open);
        Ok(())
    }

    /// Abort this stream.  This can be done from any state, and will always return the state
    /// to Idle.  If peer is not None, we are initiating this procedure and all our channels will
    /// be closed.
    pub async fn abort<'a>(&'a mut self, peer: Option<&'a Peer>) {
        if let Some(peer) = peer {
            if let Some(seid) = &self.remote_id {
                let _ = peer.abort(&seid).await;
                self.set_state(StreamState::Aborting);
            }
        }
        self.configuration.clear();
        self.remote_id = None;
        self.transport = None;
        self.set_state(StreamState::Idle);
    }

    /// Capabilities of this StreamEndpoint.
    /// Provides support for the Get Capabilities and Get All Capabilities signaling procedures.
    /// See Sections 6.7 and 6.8
    pub fn capabilities(&self) -> &Vec<ServiceCapability> {
        &self.capabilities
    }

    /// Returns the CodecType of this StreamEndpoint.
    /// Returns None if there is no MediaCodec capability in the endpoint.
    /// Note: a MediaCodec capability is required by all endpoints by the spec.
    pub fn codec_type(&self) -> Option<&MediaCodecType> {
        self.capabilities.iter().find_map(|cap| match cap {
            ServiceCapability::MediaCodec { codec_type, .. } => Some(codec_type),
            _ => None,
        })
    }

    /// Returns the local StreamEndpointId for this endpoint.
    pub fn local_id(&self) -> &StreamEndpointId {
        &self.id
    }

    /// Make a StreamInformation which represents the current state of this stream.
    pub fn information(&self) -> StreamInformation {
        StreamInformation::new(
            self.id.clone(),
            self.state != StreamState::Idle,
            self.media_type.clone(),
            self.endpoint_type.clone(),
        )
    }

    /// Take the media transport channel, which transmits (or receives) any media for this
    /// StreamEndpoint.  Returns None if the channel is held already, or if the channel has not
    /// been opened.
    pub fn take_transport(&mut self) -> Option<MediaStream> {
        let mut stream_held = self.stream_held.lock();
        if *stream_held || self.transport.is_none() {
            return None;
        }

        *stream_held = true;

        Some(MediaStream {
            in_use: self.stream_held.clone(),
            channel: Arc::downgrade(self.transport.as_ref().unwrap()),
        })
    }
}

/// Represents a media transport stream.
/// If a sink, produces the bytes that have been delivered from the peer.
/// If a source, can send bytes using `send`
pub struct MediaStream {
    in_use: Arc<Mutex<bool>>,
    channel: Weak<RwLock<Channel>>,
}

impl MediaStream {
    fn try_upgrade(&self) -> Result<Arc<RwLock<Channel>>, io::Error> {
        self.channel
            .upgrade()
            .ok_or(io::Error::new(io::ErrorKind::ConnectionAborted, "lost connection"))
    }
}

impl Drop for MediaStream {
    fn drop(&mut self) {
        let mut l = self.in_use.lock();
        *l = false;
    }
}

impl Stream for MediaStream {
    type Item = AvdtpResult<Vec<u8>>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let arc_chan = match self.try_upgrade() {
            Err(_e) => return Poll::Ready(None),
            Ok(c) => c,
        };
        let lock = match arc_chan.try_write() {
            Err(_e) => return Poll::Ready(None),
            Ok(lock) => lock,
        };
        let mut pin_chan = Pin::new(lock);
        match pin_chan.as_mut().poll_next(cx) {
            Poll::Ready(Some(Ok(res))) => Poll::Ready(Some(Ok(res))),
            Poll::Ready(Some(Err(e))) => Poll::Ready(Some(Err(Error::PeerRead(e)))),
            Poll::Ready(None) => Poll::Ready(None),
            Poll::Pending => Poll::Pending,
        }
    }
}

impl io::AsyncWrite for MediaStream {
    fn poll_write(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<Result<usize, io::Error>> {
        let arc_chan = match self.try_upgrade() {
            Err(e) => return Poll::Ready(Err(e)),
            Ok(c) => c,
        };
        let lock = match arc_chan.try_write() {
            Err(_) => {
                return Poll::Ready(Err(io::Error::new(io::ErrorKind::WouldBlock, "couldn't lock")))
            }
            Ok(lock) => lock,
        };
        let mut pin_chan = Pin::new(lock);
        pin_chan.as_mut().poll_write(cx, buf)
    }

    fn poll_flush(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        let arc_chan = match self.try_upgrade() {
            Err(e) => return Poll::Ready(Err(e)),
            Ok(c) => c,
        };
        let lock = match arc_chan.try_write() {
            Err(_) => {
                return Poll::Ready(Err(io::Error::new(io::ErrorKind::WouldBlock, "couldn't lock")))
            }
            Ok(lock) => lock,
        };
        let mut pin_chan = Pin::new(lock);
        pin_chan.as_mut().poll_flush(cx)
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        let arc_chan = match self.try_upgrade() {
            Err(e) => return Poll::Ready(Err(e)),
            Ok(c) => c,
        };
        let lock = match arc_chan.try_write() {
            Err(_) => {
                return Poll::Ready(Err(io::Error::new(io::ErrorKind::WouldBlock, "couldn't lock")))
            }
            Ok(lock) => lock,
        };
        let mut pin_chan = Pin::new(lock);
        pin_chan.as_mut().poll_close(cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        tests::{expect_remote_recv, recv_remote, setup_peer},
        types::MediaCodecType,
        Request,
    };

    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::io::AsyncWriteExt;
    use futures::stream::StreamExt;
    use matches::assert_matches;

    const REMOTE_ID_VAL: u8 = 1;
    const REMOTE_ID: StreamEndpointId = StreamEndpointId(REMOTE_ID_VAL);

    #[test]
    fn make() {
        let s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        );
        assert!(s.is_ok());
        let s = s.unwrap();
        assert_eq!(&StreamEndpointId(1), s.local_id());

        let info = s.information();
        assert!(!info.in_use());

        let no = StreamEndpoint::new(
            0,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        );
        assert!(no.is_err());
    }

    fn establish_stream(s: &mut StreamEndpoint) -> Channel {
        assert_matches!(s.establish(), Ok(()));
        let (chan, remote) = Channel::create();
        assert_matches!(s.receive_channel(chan), Ok(false));
        remote
    }

    #[test]
    fn from_info() {
        let seid = StreamEndpointId::try_from(5).unwrap();
        let info =
            StreamInformation::new(seid.clone(), false, MediaType::Audio, EndpointType::Sink);
        let capabilities = vec![ServiceCapability::MediaTransport];

        let endpoint = StreamEndpoint::from_info(&info, capabilities);

        assert_eq!(&seid, endpoint.local_id());
        assert_eq!(&false, endpoint.information().in_use());
        assert_eq!(1, endpoint.capabilities().len());
    }

    #[test]
    fn codec_type() {
        let s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![
                ServiceCapability::MediaTransport,
                ServiceCapability::MediaCodec {
                    media_type: MediaType::Audio,
                    codec_type: MediaCodecType::new(0x40),
                    codec_extra: vec![0xDE, 0xAD, 0xBE, 0xEF], // Meaningless test data.
                },
            ],
        )
        .unwrap();

        assert_eq!(Some(&MediaCodecType::new(0x40)), s.codec_type());

        let s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        )
        .unwrap();

        assert_eq!(None, s.codec_type());
    }

    #[test]
    fn stream_configure_reconfigure() {
        let _exec = fasync::Executor::new().expect("failed to create an executor");
        let mut s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![
                ServiceCapability::MediaTransport,
                ServiceCapability::MediaCodec {
                    media_type: MediaType::Audio,
                    codec_type: MediaCodecType::new(0x40),
                    codec_extra: vec![0xDE, 0xAD, 0xBE, 0xEF], // Meaningless test data.
                },
            ],
        )
        .unwrap();

        // Can't configure items that aren't in range.
        assert_matches!(
            s.configure(&REMOTE_ID, vec![ServiceCapability::Reporting]),
            Err((ServiceCategory::Reporting, ErrorCode::UnsupportedConfiguration))
        );

        assert_matches!(
            s.configure(
                &REMOTE_ID,
                vec![
                    ServiceCapability::MediaTransport,
                    ServiceCapability::MediaCodec {
                        media_type: MediaType::Audio,
                        codec_type: MediaCodecType::new(0x40),
                        // Change the codec_extra which is typical, ex. SBC (A2DP Spec 4.3.2.6)
                        codec_extra: vec![0x0C, 0x0D, 0x02, 0x51],
                    }
                ]
            ),
            Ok(())
        );

        // Note: we allow endpoints to be configured (and reconfigured) again when they
        // are only configured, even though this is probably not allowed per the spec.

        // Can't configure while open
        establish_stream(&mut s);

        assert_matches!(
            s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]),
            Err((_, ErrorCode::BadState))
        );

        let reconfiguration = vec![ServiceCapability::MediaCodec {
            media_type: MediaType::Audio,
            codec_type: MediaCodecType::new(0x40),
            // Reconfigure to yet another different codec_extra value.
            codec_extra: vec![0x0C, 0x0D, 0x0E, 0x0F],
        }];

        // The new configuration should match the previous one, but with the reconfigured
        // capabilities updated.
        let new_configuration = vec![ServiceCapability::MediaTransport, reconfiguration[0].clone()];

        // Reconfiguring while open is fine though.
        assert_matches!(s.reconfigure(reconfiguration.clone()), Ok(()));

        assert_eq!(Some(&new_configuration), s.get_configuration());

        // Can't reconfigure non-application types
        assert_matches!(
            s.reconfigure(vec![ServiceCapability::MediaTransport]),
            Err((ServiceCategory::MediaTransport, ErrorCode::InvalidCapabilities))
        );

        // Can't configure or reconfigure while streaming
        assert_matches!(s.start(), Ok(()));

        assert_matches!(
            s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]),
            Err((_, ErrorCode::BadState))
        );

        assert_matches!(s.reconfigure(reconfiguration.clone()), Err((_, ErrorCode::BadState)));

        assert_matches!(s.suspend(), Ok(()));

        // Reconfigure should be fine again in open state.
        assert_matches!(s.reconfigure(reconfiguration.clone()), Ok(()));

        // Configure is still not allowed.
        assert_matches!(
            s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]),
            Err((_, ErrorCode::BadState))
        );
    }

    #[test]
    fn stream_establishment() {
        let _exec = fasync::Executor::new().expect("failed to create an executor");
        let mut s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        )
        .unwrap();

        let (remote, transport) = Channel::create();

        // Can't establish before configuring
        assert_matches!(s.establish(), Err(ErrorCode::BadState));

        // Trying to receive a channel in the wrong state closes the channel
        assert_matches!(s.receive_channel(transport), Err(Error::InvalidState));

        let buf: &mut [u8] = &mut [0; 1];

        assert_matches!(remote.as_ref().read(buf), Err(zx::Status::PEER_CLOSED));

        assert_matches!(s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]), Ok(()));

        assert_matches!(s.establish(), Ok(()));

        // And we should be able to give a channel now.
        let (_remote, transport) = Channel::create();
        assert_matches!(s.receive_channel(transport), Ok(false));
    }

    fn setup_peer_for_release(exec: &mut fasync::Executor) -> (Peer, Channel, SimpleResponder) {
        let (peer, signaling) = setup_peer();
        // Send a close from the other side to produce an event we can respond to.
        signaling.as_ref().write(&[0x40, 0x08, 0x04]).expect("signaling write");
        let mut req_stream = peer.take_request_stream();
        let mut req_fut = req_stream.next();
        let complete = exec.run_until_stalled(&mut req_fut);
        let responder = match complete {
            Poll::Ready(Some(Ok(Request::Close { responder, .. }))) => responder,
            _ => panic!("Expected a close request"),
        };
        (peer, signaling, responder)
    }

    #[test]
    fn stream_release_without_abort() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        )
        .unwrap();

        assert_matches!(s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]), Ok(()));

        let remote_transport = establish_stream(&mut s);

        let (peer, signaling, responder) = setup_peer_for_release(&mut exec);

        let mut release_fut = Box::pin(s.release(responder, &peer));
        let complete = exec.run_until_stalled(&mut release_fut);

        // We should still be pending since the transport hasn't been closed.
        assert!(complete.is_pending());

        // Expect a "yes" response.
        expect_remote_recv(&[0x42, 0x08], &signaling);

        // Close the transport channel by dropping it.
        drop(remote_transport);

        // After the transport is closed the release future should be complete.
        assert_matches!(exec.run_until_stalled(&mut release_fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_mediastream() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        )
        .unwrap();

        assert_matches!(s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]), Ok(()));

        // Before the stream is opened, we shouldn't be able to take the transport.
        assert!(s.take_transport().is_none());

        let remote_transport = establish_stream(&mut s);

        // Should be able to get the transport from the stream now.
        let temp_stream = s.take_transport();
        assert!(temp_stream.is_some());

        // But only once
        assert!(s.take_transport().is_none());

        // Until you drop the stream
        drop(temp_stream);

        let media_stream = s.take_transport();
        assert!(media_stream.is_some());
        let mut media_stream = media_stream.unwrap();

        // Writing to the media stream should send it through the transport channel.
        let hearts = &[0xF0, 0x9F, 0x92, 0x96, 0xF0, 0x9F, 0x92, 0x96];
        let mut write_fut = media_stream.write(hearts);

        assert_matches!(exec.run_until_stalled(&mut write_fut), Poll::Ready(Ok(8)));

        expect_remote_recv(hearts, &remote_transport);

        // Closing the media stream should close the channel.
        let mut close_fut = media_stream.close();
        assert_matches!(exec.run_until_stalled(&mut close_fut), Poll::Ready(Ok(())));
        // Note: there's no effect on the other end of the channel when a close occurs,
        // until the channel is dropped.

        drop(s);

        // Reading from the remote end should fail.
        let mut result = vec![0];
        assert_matches!(
            remote_transport.as_ref().read(&mut result[..]),
            Err(zx::Status::PEER_CLOSED)
        );

        // After the stream is gone, any write should return an Err
        let mut write_fut = media_stream.write(&[0xDE, 0xAD]);
        assert_matches!(exec.run_until_stalled(&mut write_fut), Poll::Ready(Err(_)));

        // After the stream is gone, the stream should be fused done.
        let mut next_fut = media_stream.next();
        assert_matches!(exec.run_until_stalled(&mut next_fut), Poll::Ready(None));
    }

    #[test]
    fn stream_release_with_abort() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        )
        .unwrap();

        assert_matches!(s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]), Ok(()));
        let _remote_transport = establish_stream(&mut s);
        let (peer, signaling, responder) = setup_peer_for_release(&mut exec);

        let mut release_fut = Box::pin(s.release(responder, &peer));
        let complete = exec.run_until_stalled(&mut release_fut);

        // We should still be pending since the transport hasn't been closed.
        assert!(complete.is_pending());

        // Expect a "yes" response.
        expect_remote_recv(&[0x42, 0x08], &signaling);

        // TODO(jamuraa): We need to wait until the timer expires for now.
        exec.wake_next_timer();
        let complete = exec.run_until_stalled(&mut release_fut);
        // Now we're waiting on response from the Abort
        assert!(complete.is_pending());
        // Should have got an abort
        let received = recv_remote(&signaling).unwrap();
        assert_eq!(0x0A, received[1]);
        let txlabel = received[0] & 0xF0;
        // Send a response
        assert!(signaling.as_ref().write(&[txlabel | 0x02, 0x0A]).is_ok());

        assert_matches!(exec.run_until_stalled(&mut release_fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn start_and_suspend() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        )
        .unwrap();

        // Can't start or suspend until configured and open.
        assert_matches!(s.start(), Err(ErrorCode::BadState));
        assert_matches!(s.suspend(), Err(ErrorCode::BadState));

        assert_matches!(s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]), Ok(()));

        assert_matches!(s.start(), Err(ErrorCode::BadState));
        assert_matches!(s.suspend(), Err(ErrorCode::BadState));

        assert_matches!(s.establish(), Ok(()));

        assert_matches!(s.start(), Err(ErrorCode::BadState));
        assert_matches!(s.suspend(), Err(ErrorCode::BadState));

        let (remote, transport) = Channel::create();
        assert_matches!(s.receive_channel(transport), Ok(false));

        // Should be able to start but not suspend now.
        assert_matches!(s.suspend(), Err(ErrorCode::BadState));
        assert_matches!(s.start(), Ok(()));

        // Are started, so we should be able to suspend but not start again here.
        assert_matches!(s.start(), Err(ErrorCode::BadState));
        assert_matches!(s.suspend(), Ok(()));

        // Now we're suspended, so we can start it again.
        assert_matches!(s.start(), Ok(()));
        assert_matches!(s.suspend(), Ok(()));

        // After we close, we are back at idle and can't start / stop
        let (peer, signaling, responder) = setup_peer_for_release(&mut exec);

        {
            let mut release_fut = Box::pin(s.release(responder, &peer));
            let complete = exec.run_until_stalled(&mut release_fut);

            // We should still be pending since the transport hasn't been closed.
            assert!(complete.is_pending());

            // Expect a "yes" response.
            expect_remote_recv(&[0x42, 0x08], &signaling);

            // Close the transport channel by dropping it.
            drop(remote);

            // After the channel is closed we should be done.
            assert_matches!(exec.run_until_stalled(&mut release_fut), Poll::Ready(Ok(())));
        }

        // Shouldn't be able to start or suspend again.
        assert_matches!(s.start(), Err(ErrorCode::BadState));
        assert_matches!(s.suspend(), Err(ErrorCode::BadState));
    }

    #[test]
    fn get_configuration() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![
                ServiceCapability::MediaTransport,
                ServiceCapability::Reporting,
                ServiceCapability::MediaCodec {
                    media_type: MediaType::Audio,
                    codec_type: MediaCodecType::new(0),
                    // Nonesense codec information elements.
                    codec_extra: vec![0x60, 0x0D, 0xF0, 0x0D],
                },
            ],
        )
        .unwrap();

        // Can't get configuration if we aren't configured.
        assert!(s.get_configuration().is_none());

        let config = vec![
            ServiceCapability::MediaTransport,
            ServiceCapability::MediaCodec {
                media_type: MediaType::Audio,
                codec_type: MediaCodecType::new(0),
                // Change the codec_extra which is typical, ex. SBC (A2DP Spec 4.3.2.6)
                codec_extra: vec![0x60, 0x0D, 0x02, 0x55],
            },
        ];

        assert_matches!(s.configure(&REMOTE_ID, config.clone()), Ok(()));

        match s.get_configuration() {
            Some(c) => assert_eq!(&config, c),
            x => panic!("Expected Ok from get_configuration but got {:?}", x),
        };

        {
            // Abort this stream, putting it back to the idle state.
            let mut abort_fut = Box::pin(s.abort(None));
            let complete = exec.run_until_stalled(&mut abort_fut);
            assert_matches!(complete, Poll::Ready(()));
        }

        assert!(s.get_configuration().is_none());
    }

    use std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    };

    /// Create a callback that tracks how many times it has been called
    fn call_count_callback() -> (Option<StreamEndpointUpdateCallback>, Arc<AtomicUsize>) {
        let call_count = Arc::new(AtomicUsize::new(0));
        let call_count_reader = call_count.clone();
        let count_cb: StreamEndpointUpdateCallback = Box::new(move |_stream: &StreamEndpoint| {
            call_count.fetch_add(1, Ordering::SeqCst);
        });
        (Some(count_cb), call_count_reader)
    }

    /// Test that the update callback is run at least once for all methods that mutate the state of
    /// the StreamEndpoint. This is done through an atomic counter in the callback that increments
    /// when the callback is run.
    ///
    /// Note that the _results_ of calling these mutating methods on the state of StreamEndpoint are
    /// not validated here. They are validated in other tests.
    #[test]
    fn update_callback() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut s = StreamEndpoint::new(
            REMOTE_ID_VAL,
            MediaType::Audio,
            EndpointType::Sink,
            vec![ServiceCapability::MediaTransport],
        )
        .unwrap();
        let (cb, call_count) = call_count_callback();
        s.set_update_callback(cb);

        s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport])
            .expect("Configure to succeed in test");
        assert!(call_count.load(Ordering::SeqCst) > 0, "Update callback called at least once");
        call_count.store(0, Ordering::SeqCst); // clear call count

        s.establish().expect("Establish to succeed in test");
        assert!(call_count.load(Ordering::SeqCst) > 0, "Update callback called at least once");
        call_count.store(0, Ordering::SeqCst); // clear call count

        let (_, transport) = Channel::create();
        s.receive_channel(transport).expect("Receive channel to succeed in test");
        assert!(call_count.load(Ordering::SeqCst) > 0, "Update callback called at least once");
        call_count.store(0, Ordering::SeqCst); // clear call count

        s.start().expect("Start to succeed in test");
        assert!(call_count.load(Ordering::SeqCst) > 0, "Update callback called at least once");
        call_count.store(0, Ordering::SeqCst); // clear call count

        s.suspend().expect("Suspend to succeed in test");
        assert!(call_count.load(Ordering::SeqCst) > 0, "Update callback called at least once");
        call_count.store(0, Ordering::SeqCst); // clear call count

        s.reconfigure(vec![]).expect("Reconfigure to succeed in test");
        assert!(call_count.load(Ordering::SeqCst) > 0, "Update callback called at least once");
        call_count.store(0, Ordering::SeqCst); // clear call count

        {
            // Abort this stream, putting it back to the idle state.
            let mut abort_fut = Box::pin(s.abort(None));
            let _ = exec.run_until_stalled(&mut abort_fut);
            assert!(call_count.load(Ordering::SeqCst) > 0, "Update callback called at least once");
        }
    }
}
