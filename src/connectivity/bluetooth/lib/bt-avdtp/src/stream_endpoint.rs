// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use {
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_zircon::{Duration, Signals, Status, Time},
    futures::{stream::Stream, task::Waker, Poll},
    parking_lot::Mutex,
    std::{pin::Pin, sync::Arc, sync::Weak},
};

use crate::{
    types::{
        EndpointType, Error, ErrorCode, MediaType, Result, ServiceCapability, StreamEndpointId,
        StreamInformation, TryFrom,
    },
    Peer, SimpleResponder,
};

#[derive(PartialEq)]
enum StreamState {
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
    /// The media transport socket.
    /// This should be Some(socket) when state is Open or Streaming.
    transport: Option<Arc<fasync::Socket>>,
    /// True when the MediaStream is held.
    /// Prevents multiple threads from owning the media stream.
    stream_held: Arc<Mutex<bool>>,
    /// The capabilities of this endpoint.
    capabilities: Vec<ServiceCapability>,
    /// The remote stream endpoint id.  None if the stream has never been configured.
    remote_id: Option<StreamEndpointId>,
    /// The current configuration of this endpoint.  Empty if the stream has never been configured.
    configuration: Vec<ServiceCapability>,
}

impl StreamEndpoint {
    /// Make a new StreamEndpoint.
    /// |id| must be in the valid range for a StreamEndpointId (0x01 - 0x3E).
    /// StreamEndpooints start in the Idle state.
    pub fn new(
        id: u8,
        media_type: MediaType,
        endpoint_type: EndpointType,
        capabilities: Vec<ServiceCapability>,
    ) -> Result<StreamEndpoint> {
        let seid = StreamEndpointId::try_from(id)?;
        Ok(StreamEndpoint {
            id: seid,
            capabilities: capabilities,
            media_type: media_type,
            endpoint_type: endpoint_type,
            state: StreamState::Idle,
            transport: None,
            stream_held: Arc::new(Mutex::new(false)),
            remote_id: None,
            configuration: vec![],
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

    /// Attempt to Configure this stream using the capabilities given.
    /// If the stream is not in an Idle state, fails with Err(InvalidState).
    /// Used for the Stream Configuration procedure, see Section 6.9
    pub fn configure(
        &mut self,
        remote_id: &StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
    ) -> Result<()> {
        if self.state != StreamState::Idle {
            return Err(Error::InvalidState);
        }
        self.remote_id = Some(remote_id.clone());
        for cap in &capabilities {
            if !self
                .capabilities
                .iter()
                .any(|y| std::mem::discriminant(cap) == std::mem::discriminant(y))
            {
                return Err(Error::OutOfRange);
            }
        }
        self.configuration = capabilities;
        self.state = StreamState::Configured;
        Ok(())
    }

    /// Attempt to reconfigure this stream with the capabilities given.
    /// If the capabilities are not valid to set, fails with Err(OutOfRange)
    /// If the stream is not in the Open state, fails with Err(InvalidState)
    /// Used for the Stream Reconfiguration procedure, see Section 6.15.
    pub fn reconfigure(&mut self, mut capabilities: Vec<ServiceCapability>) -> Result<()> {
        if self.state != StreamState::Open {
            return Err(Error::InvalidState);
        }
        // Only application capabilities are allowed to be reconfigured. See Section 8.11.1
        if capabilities.iter().any(|x| !x.is_application()) {
            return Err(Error::OutOfRange);
        }
        // Should only replace the capabilities that have been reconfigured. See Section 8.11.2
        let to_replace: std::vec::Vec<_> =
            capabilities.iter().map(|x| std::mem::discriminant(x)).collect();
        self.capabilities.retain(|x| {
            let disc = std::mem::discriminant(x);
            !to_replace.contains(&disc)
        });
        self.capabilities.append(&mut capabilities);
        Ok(())
    }

    /// Get the current configuration of this stream.
    /// If the stream is in the Idle, Closing, or Aborting state, this shall
    /// fail.
    /// Used for the Steam Get Configuration Procedure, see Section 6.10
    pub fn get_configuration(&self) -> Result<Vec<ServiceCapability>> {
        if self.state.configured() {
            Ok(self.capabilities.clone())
        } else {
            Err(Error::InvalidState)
        }
    }

    /// When a L2CAP channel is received after an Open command is accepted, it should be
    /// delivered via receive_channel.
    /// Returns true if this Endpoint expects more channels to be established before
    /// streaming is started.
    /// Returns Err(InvalidState) if this Endpoint is not expecting a channel to be established,
    /// closing |c|.
    pub fn receive_channel(&mut self, c: fasync::Socket) -> Result<bool> {
        if self.state != StreamState::Opening || self.transport.is_some() {
            return Err(Error::InvalidState);
        }
        self.transport = Some(Arc::new(c));
        self.stream_held = Arc::new(Mutex::new(false));
        // TODO(jamuraa, NET-1674, NET-1675): Reporting and Recovery channels
        self.state = StreamState::Open;
        Ok(false)
    }

    /// Begin opening this stream.  The stream must be in a Configured state.
    /// See Stream Establishment, Section 6.11
    pub fn establish(&mut self) -> Result<()> {
        if self.state != StreamState::Configured || self.transport.is_some() {
            return Err(Error::InvalidState);
        }
        self.state = StreamState::Opening;
        Ok(())
    }

    /// Close this stream.  This procedure checks that the media channels are closed.
    /// If the channels are not closed in 3 seconds, it initates an abort prodecure with the
    /// remote |peer| and returns the result of that.
    pub async fn release<'a>(
        &'a mut self,
        responder: SimpleResponder,
        peer: &'a Peer,
    ) -> Result<()> {
        if self.state != StreamState::Open && self.state != StreamState::Streaming {
            return responder.reject(ErrorCode::BadState);
        }
        self.state = StreamState::Closing;
        responder.send()?;
        if let Some(sock) = &self.transport {
            let timeout = Time::after(Duration::from_seconds(3));

            let close_signals = Signals::SOCKET_PEER_CLOSED;
            let close_wait = fasync::OnSignals::new(sock.as_ref(), close_signals);

            match await!(close_wait.on_timeout(timeout, || { Err(Status::TIMED_OUT) })) {
                Err(Status::TIMED_OUT) => return await!(self.abort(Some(peer))),
                _ => (),
            };
        }
        // Closing returns this endpoint to the Idle state.
        self.configuration.clear();
        self.remote_id = None;
        self.state = StreamState::Idle;
        Ok(())
    }

    /// Start this stream.  This can be done only from the Open State.
    /// Used for the Stream Start procedure, See Section 6.12
    pub fn start(&mut self) -> Result<()> {
        if self.state != StreamState::Open {
            return Err(Error::InvalidState);
        }
        self.state = StreamState::Streaming;
        Ok(())
    }

    /// Suspend this stream.  This can be done only from the Streaming state.
    /// Used for the Stream Suspend procedure, See Section 6.14
    pub fn suspend(&mut self) -> Result<()> {
        if self.state != StreamState::Streaming {
            return Err(Error::InvalidState);
        }
        self.state = StreamState::Open;
        Ok(())
    }

    /// Abort this stream.  This can be done from any state, and will always return the state
    /// to Idle.  If peer is not None, we are initiating this procedure and all our channels will
    /// be closed.
    pub async fn abort<'a>(&'a mut self, peer: Option<&'a Peer>) -> Result<()> {
        if let Some(peer) = peer {
            if let Some(seid) = &self.remote_id {
                let _ = await!(peer.abort(&seid));
                self.state = StreamState::Aborting;
            }
        }
        self.configuration.clear();
        self.remote_id = None;
        self.transport = None;
        self.state = StreamState::Idle;
        Ok(())
    }

    /// Capabilities of this StreamEndpoint.
    /// Provides support for the Get Capabilities and Get All Capabilities signaling procedures.
    /// See Sections 6.7 and 6.8
    pub fn capabilities(&self) -> &Vec<ServiceCapability> {
        &self.capabilities
    }

    /// Returns the local StreamEndpointId for this endpoint.
    pub fn local_id(&self) -> &StreamEndpointId {
        &self.id
    }

    /// Make a StreamInforamtion which represents the current state of this stream.
    pub fn information(&self) -> StreamInformation {
        StreamInformation::new(
            self.id.clone(),
            self.state != StreamState::Idle,
            self.media_type.clone(),
            self.endpoint_type.clone(),
        )
    }

    /// Take the media stream, which transmits (or receives) any media for this StreamEndpoint.
    /// Panics if the media stream is alraedy taken, or if the stream is not open.
    pub fn take_transport(&mut self) -> MediaStream {
        let mut lock = self.stream_held.lock();
        assert!(!*lock && self.transport.is_some(), "Media stream has already been taken.");

        *lock = true;

        MediaStream {
            lock: self.stream_held.clone(),
            stream: Arc::downgrade(self.transport.as_ref().unwrap()),
        }
    }
}

/// Represents the stream of media.
/// Currently just a weak pointer to the transport socket.
/// TODO(jamuraa): parse transport sockets, make this into a real asynchronous socket that
/// produces and/or takes media frames.
pub struct MediaStream {
    lock: Arc<Mutex<bool>>,
    stream: Weak<fasync::Socket>,
}

impl Drop for MediaStream {
    fn drop(&mut self) {
        let mut l = self.lock.lock();
        *l = false;
    }
}

impl Stream for MediaStream {
    type Item = Result<Vec<u8>>;

    fn poll_next(self: Pin<&mut Self>, lw: &Waker) -> Poll<Option<Self::Item>> {
        let s = match self.stream.upgrade() {
            None => return Poll::Ready(None),
            Some(s) => s,
        };
        let mut res = Vec::<u8>::new();
        match s.poll_datagram(&mut res, lw) {
            Poll::Ready(Ok(_size)) => Poll::Ready(Some(Ok(res))),
            Poll::Ready(Err(e)) => Poll::Ready(Some(Err(Error::PeerRead(e)))),
            Poll::Pending => Poll::Pending,
        }
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

    use fuchsia_zircon as zx;
    use futures::stream::StreamExt;

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

    fn establish_stream(s: &mut StreamEndpoint) -> zx::Socket {
        assert_eq!(Ok(()), s.establish());
        let (remote, transport) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        assert_eq!(Ok(false), s.receive_channel(fasync::Socket::from_socket(transport).unwrap()));
        remote
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
                    codec_extra: vec![0xDE, 0xAD, 0xBE, 0xEF],
                },
            ],
        )
        .unwrap();

        // Can't configure items that aren't in range.
        assert_eq!(
            Err(Error::OutOfRange),
            s.configure(&REMOTE_ID, vec![ServiceCapability::Reporting])
        );

        assert_eq!(
            Ok(()),
            s.configure(
                &REMOTE_ID,
                vec![
                    ServiceCapability::MediaTransport,
                    ServiceCapability::MediaCodec {
                        media_type: MediaType::Audio,
                        codec_type: MediaCodecType::new(0x40),
                        codec_extra: vec![0x0C, 0x0D, 0x0E, 0x0F],
                    }
                ]
            )
        );

        // Note: we allow devices to be configured (and reconfigured) again when they are
        // just configured, even though this is proabably not allowed per the spec.

        // Can't configure while open
        establish_stream(&mut s);

        assert_eq!(
            Err(Error::InvalidState),
            s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport])
        );

        // Reconfiguring while open is fine though.
        assert_eq!(
            Ok(()),
            s.reconfigure(vec![ServiceCapability::MediaCodec {
                media_type: MediaType::Audio,
                codec_type: MediaCodecType::new(0x40),
                codec_extra: vec![0x0C, 0x0D, 0x0E, 0x0F],
            }])
        );

        // Can't reconfigure non-application types
        assert_eq!(Err(Error::OutOfRange), s.reconfigure(vec![ServiceCapability::MediaTransport]));

        // Can't configure or reconfigure while streaming
        assert_eq!(Ok(()), s.start());

        assert_eq!(
            Err(Error::InvalidState),
            s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport])
        );

        assert_eq!(
            Err(Error::InvalidState),
            s.reconfigure(vec![ServiceCapability::MediaCodec {
                media_type: MediaType::Audio,
                codec_type: MediaCodecType::new(0x40),
                codec_extra: vec![0x0C, 0x0D, 0x0E, 0x0F],
            }])
        );

        assert_eq!(Ok(()), s.suspend());

        // Reconfigure should be fine again in open state.
        assert_eq!(
            Ok(()),
            s.reconfigure(vec![ServiceCapability::MediaCodec {
                media_type: MediaType::Audio,
                codec_type: MediaCodecType::new(0x40),
                codec_extra: vec![0x0C, 0x0D, 0x0E, 0x0F],
            }])
        );

        // Configure is stil not allowed.
        assert_eq!(
            Err(Error::InvalidState),
            s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport])
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

        let (remote, transport) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

        // Can't establish before configuring
        assert_eq!(Err(Error::InvalidState), s.establish());

        // Trying to receive a channel in the wrong state closes the channel
        assert_eq!(
            Err(Error::InvalidState),
            s.receive_channel(fasync::Socket::from_socket(transport).unwrap())
        );

        let buf: &mut [u8] = &mut [0; 1];

        assert_eq!(Err(zx::Status::PEER_CLOSED), remote.read(buf));

        assert_eq!(Ok(()), s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]));

        assert_eq!(Ok(()), s.establish());

        // And we should be able to give a channel now.
        let (_remote, transport) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        assert_eq!(Ok(false), s.receive_channel(fasync::Socket::from_socket(transport).unwrap()));
    }

    fn setup_peer_for_release(exec: &mut fasync::Executor) -> (Peer, zx::Socket, SimpleResponder) {
        let (peer, signaling) = setup_peer();
        // Send a close from the other side to produce an event we can respond to.
        signaling.write(&[0x40, 0x08, 0x04]).is_ok();
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

        assert_eq!(Ok(()), s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]));

        let remote_transport = establish_stream(&mut s);
        let (peer, signaling, responder) = setup_peer_for_release(&mut exec);

        let mut release_fut = Box::pin(s.release(responder, &peer));
        let complete = exec.run_until_stalled(&mut release_fut);

        // We should still be pending since the transport hasn't been closed.
        assert!(complete.is_pending());

        // Expect a "yes" response.
        expect_remote_recv(&[0x42, 0x08], &signaling);

        // Close the transport socket by dropping it.
        drop(remote_transport);

        // After the transport is closed the release future should be complete.
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut release_fut));
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

        assert_eq!(Ok(()), s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]));
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
        // Now we're wairing on response from the Abort
        assert!(complete.is_pending());
        // Should have got an abort
        let received = recv_remote(&signaling).unwrap();
        assert_eq!(0x0A, received[1]);
        let txlabel = received[0] & 0xF0;
        // Send a response
        assert!(signaling.write(&[txlabel | 0x02, 0x0A]).is_ok());

        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut release_fut));
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
        assert_eq!(Err(Error::InvalidState), s.start());
        assert_eq!(Err(Error::InvalidState), s.suspend());

        assert_eq!(Ok(()), s.configure(&REMOTE_ID, vec![ServiceCapability::MediaTransport]));

        assert_eq!(Err(Error::InvalidState), s.start());
        assert_eq!(Err(Error::InvalidState), s.suspend());

        assert_eq!(Ok(()), s.establish());

        assert_eq!(Err(Error::InvalidState), s.start());
        assert_eq!(Err(Error::InvalidState), s.suspend());

        let (remote, transport) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        assert_eq!(Ok(false), s.receive_channel(fasync::Socket::from_socket(transport).unwrap()));

        // Should be able to start but not suspend now.
        assert_eq!(Err(Error::InvalidState), s.suspend());
        assert_eq!(Ok(()), s.start());

        // Are started, so we should be able to suspend but not start again here.
        assert_eq!(Err(Error::InvalidState), s.start());
        assert_eq!(Ok(()), s.suspend());

        // Now we're suspended, so we can start it again.
        assert_eq!(Ok(()), s.start());
        assert_eq!(Ok(()), s.suspend());

        // After we close, we are back at idle and can't start / stop
        let (peer, signaling, responder) = setup_peer_for_release(&mut exec);

        {
            let mut release_fut = Box::pin(s.release(responder, &peer));
            let complete = exec.run_until_stalled(&mut release_fut);

            // We should still be pending since the transport hasn't been closed.
            assert!(complete.is_pending());

            // Expect a "yes" response.
            expect_remote_recv(&[0x42, 0x08], &signaling);

            // Close the transport socket by dropping it.
            drop(remote);

            // After the socket is closed we should be done.
            assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut release_fut));
        }

        // Shouldn't be able to start or suspend again.
        assert_eq!(Err(Error::InvalidState), s.start());
        assert_eq!(Err(Error::InvalidState), s.suspend());
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
                ServiceCapability::MediaCodec {
                    media_type: MediaType::Audio,
                    codec_type: MediaCodecType::new(0),
                    codec_extra: vec![0x60, 0x0D, 0xF0, 0x0D],
                },
            ],
        )
        .unwrap();

        // Can't get configuration if we aren't configured.
        assert_eq!(Err(Error::InvalidState), s.get_configuration());

        let config = vec![
            ServiceCapability::MediaTransport,
            ServiceCapability::MediaCodec {
                media_type: MediaType::Audio,
                codec_type: MediaCodecType::new(0),
                codec_extra: vec![0x60, 0x0D, 0xF0, 0x0D],
            },
        ];

        assert_eq!(Ok(()), s.configure(&REMOTE_ID, config.clone()));

        assert_eq!(Ok(config), s.get_configuration());

        {
            // Abort this stream, putting it back to the idle state.
            let mut abort_fut = Box::pin(s.abort(None));
            let complete = exec.run_until_stalled(&mut abort_fut);
            assert_eq!(Poll::Ready(Ok(())), complete);
        }

        assert_eq!(Err(Error::InvalidState), s.get_configuration());
    }
}
