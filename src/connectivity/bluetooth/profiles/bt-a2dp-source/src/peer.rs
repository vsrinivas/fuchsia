// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avdtp::{self as avdtp, ServiceCapability, StreamEndpoint, StreamEndpointId},
    fidl_fuchsia_bluetooth_bredr::{ProfileDescriptor, ProfileProxy, PSM_AVDTP},
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{Future, StreamExt},
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc},
};

struct Streams(HashMap<StreamEndpointId, StreamEndpoint>);

impl Streams {
    /// A new empty set of endpoints.
    fn new() -> Streams {
        Streams(HashMap::new())
    }

    /// Adds a stream, indexing it by the endpoint id, associated with an encoding,
    /// replacing any other stream with the same endpoint id.
    fn insert(&mut self, stream: avdtp::StreamEndpoint) {
        self.0.insert(stream.local_id().clone(), stream);
    }

    /// Retrieves a reference to the Stream referenced by `id`, if the stream exists,
    /// otherwise returns Err(BadAcpSeid).
    fn get(&mut self, id: &StreamEndpointId) -> Option<&StreamEndpoint> {
        self.0.get(id)
    }

    /// Retrieves a mutable reference to the Stream referenced by `id`, if the stream exists,
    /// otherwise returns Err(BadAcpSeid).
    fn get_mut(&mut self, id: &StreamEndpointId) -> Option<&mut StreamEndpoint> {
        self.0.get_mut(id)
    }

    /// Returns the information on all known streams.
    fn information(&self) -> Vec<avdtp::StreamInformation> {
        self.0.values().map(|x| x.information()).collect()
    }
}

pub struct Peer {
    /// The id of the peer we are connected to.
    id: PeerId,
    /// The inner peer
    inner: Arc<Mutex<PeerInner>>,
    /// Profile Proxy, used to connect new transport sockets.
    profile: ProfileProxy,
    /// The profile descriptor for this peer, if it has been discovered.
    descriptor: Mutex<Option<ProfileDescriptor>>,
    // TODO(39730): Add a future for when the peer closes?
}

impl Peer {
    /// Make a new Peer which is connected to the peer `id` using the AVDTP `peer`.
    /// The `streams` are the local endpoints available to the peer.
    /// `profile` will be used to initiate connections for Media Transport.
    /// This also starts a task on the executor to handle incoming events from the peer.
    pub fn create(
        id: PeerId,
        peer: avdtp::Peer,
        streams: Vec<avdtp::StreamEndpoint>,
        profile: ProfileProxy,
    ) -> Self {
        let res = Self {
            id,
            inner: Arc::new(Mutex::new(PeerInner::new(peer, streams))),
            profile,
            descriptor: Mutex::new(None),
        };
        res.start_requests_task();
        res
    }

    pub fn set_descriptor(&self, descriptor: ProfileDescriptor) -> Option<ProfileDescriptor> {
        self.descriptor.lock().replace(descriptor)
    }

    /// Receive a channel from the peer that was initiated remotely.
    /// This function should be called whenever the peer associated with this opens an L2CAP channel.
    pub fn receive_channel(&self, channel: zx::Socket) -> avdtp::Result<()> {
        let mut lock = self.inner.lock();
        lock.receive_channel(channel)
    }

    /// Return a handle to the AVDTP peer, to use as initiator of commands.
    pub fn avdtp_peer(&self) -> Arc<avdtp::Peer> {
        let lock = self.inner.lock();
        lock.peer.clone()
    }

    /// Perform Discovery and then Capability detection to discover the endpoints and capabilities of the
    /// connected peer.
    /// Returns a vector of remote stream endpoints.
    /// the endpoint.
    pub fn collect_capabilities(
        &self,
    ) -> impl Future<Output = avdtp::Result<Vec<avdtp::StreamEndpoint>>> {
        let avdtp = self.inner.lock().peer.clone();
        let get_all = self.descriptor.lock().map_or(false, a2dp_version_check);
        async move {
            fx_vlog!(1, "Discovering peer streams..");
            let infos = avdtp.discover().await?;
            fx_vlog!(1, "Discovered {} streams", infos.len());
            let mut remote_streams = Vec::new();
            for info in infos {
                let capabilities = if get_all {
                    avdtp.get_all_capabilities(info.id()).await
                } else {
                    avdtp.get_capabilities(info.id()).await
                };
                match capabilities {
                    Ok(capabilities) => {
                        fx_vlog!(1, "Stream {:?}", info);
                        for cap in &capabilities {
                            fx_vlog!(1, "  - {:?}", cap);
                        }
                        remote_streams.push(avdtp::StreamEndpoint::from_info(&info, capabilities));
                    }
                    Err(e) => {
                        fx_log_info!("Stream {} capabilities failed: {:?}, skipping", info.id(), e);
                    }
                };
            }
            Ok(remote_streams)
        }
    }

    /// Open and start a media transport stream, connecting the local stream `local_id` to the
    /// remote stream `remote_id`, configuring it with the MediaCodec capability.
    /// Returns the MediaStream which can either be streamed from, or an error.
    pub fn start_stream(
        &self,
        local_id: StreamEndpointId,
        remote_id: StreamEndpointId,
        codec_params: ServiceCapability,
    ) -> impl Future<Output = avdtp::Result<avdtp::MediaStream>> {
        let peer = Arc::downgrade(&self.inner);
        let lock = self.inner.lock();
        let peer_id = self.id.clone();
        let avdtp = lock.peer.clone();
        let profile = self.profile.clone();

        async move {
            fx_vlog!(
                1,
                "Connecting stream {} to remote stream {} with {:?}",
                local_id,
                remote_id,
                codec_params
            );

            let capabilities = vec![ServiceCapability::MediaTransport, codec_params];

            avdtp.set_configuration(&remote_id, &local_id, &capabilities).await?;
            avdtp.open(&remote_id).await?;

            {
                let strong = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                strong.lock().set_opening(&local_id, &remote_id, capabilities)?;
            }

            let (status, channel) = profile
                .connect_l2cap(&peer_id.to_string(), PSM_AVDTP as u16)
                .await
                .expect("FIDL error: {}");
            if let Some(e) = status.error {
                fx_log_warn!("Couldn't connect media transport {}: {:?}", peer_id, e);
                return Err(avdtp::Error::PeerDisconnected);
            }
            if channel.is_none() {
                fx_log_warn!("Couldn't connect media transport {}: no channel", peer_id);
                return Err(avdtp::Error::PeerDisconnected);
            }

            {
                let strong_peer = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                let mut strong_peer = strong_peer.lock();
                strong_peer.receive_channel(channel.unwrap())?;
            }

            let to_start = &[remote_id];
            avdtp.start(to_start).await?;

            {
                let strong_peer = peer.upgrade().ok_or(avdtp::Error::PeerDisconnected)?;
                let mut strong_peer = strong_peer.lock();
                strong_peer.set_started(&local_id)?;
                Ok(strong_peer.take_media_stream(&local_id)?)
            }
        }
    }

    /// Start an asynchronous task to handle any requests from the AVDTP peer.
    /// This task completes when the remote end closes the signaling connection.
    fn start_requests_task(&self) {
        let lock = self.inner.lock();
        let mut request_stream = lock.peer.take_request_stream();
        let id = self.id.clone();
        let peer = Arc::downgrade(&self.inner);
        fuchsia_async::spawn_local(async move {
            while let Some(r) = request_stream.next().await {
                match r {
                    Err(e) => fx_log_info!("Request Error on {}: {:?}", id, e),
                    Ok(request) => match peer.upgrade() {
                        None => {
                            fx_log_info!("Peer disappeared processing requests, ending");
                            return;
                        }
                        Some(p) => {
                            let mut lock = p.lock();
                            if let Err(e) = lock.handle_request(request).await {
                                fx_log_warn!("{} Error handling request: {:?}", id, e);
                            }
                        }
                    },
                }
            }
            fx_log_info!("Peer {} disconnected", id);
        });
    }
}

/// Determines if Peer profile version is newer (>= 1.3) or older (< 1.3)
fn a2dp_version_check(profile: ProfileDescriptor) -> bool {
    (profile.major_version == 1 && profile.minor_version >= 3) || profile.major_version > 1
}

/// Peer handles the communicaton with the AVDTP layer, and provides responses as appropriate
/// based on the current state of local streams available.
/// Each peer has its own set of local stream endpoints, and tracks a set of remote peer endpoints.
struct PeerInner {
    /// AVDTP peer communicating to this.
    peer: Arc<avdtp::Peer>,
    /// Some(id) if we are opening a StreamEndpoint but haven't finished yet.
    /// This is the local ID.
    /// AVDTP Sec 6.11 - only up to one stream can be in this state.
    opening: Option<StreamEndpointId>,
    /// The local stream endpoint collection
    local: Streams,
}

impl PeerInner {
    pub fn new(peer: avdtp::Peer, streams: Vec<avdtp::StreamEndpoint>) -> Self {
        let mut local = Streams::new();
        for stream in streams {
            local.insert(stream);
        }
        Self { peer: Arc::new(peer), opening: None, local }
    }

    /// Returns an endpoint from the local set or a BadAcpSeid error if it doesn't exist.
    fn get_mut(&mut self, local_id: &StreamEndpointId) -> avdtp::Result<&mut StreamEndpoint> {
        self.local
            .get_mut(&local_id)
            .ok_or(avdtp::Error::RequestInvalid(avdtp::ErrorCode::BadAcpSeid))
    }

    fn set_opening(
        &mut self,
        local_id: &StreamEndpointId,
        remote_id: &StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
    ) -> avdtp::Result<()> {
        let stream = self.get_mut(&local_id)?;
        stream.configure(&remote_id, capabilities)?;
        stream.establish()?;
        self.opening = Some(local_id.clone());
        Ok(())
    }

    fn set_started(&mut self, local_id: &StreamEndpointId) -> avdtp::Result<()> {
        let stream = self.get_mut(&local_id)?;
        fx_log_info!("Setting started: {:?}", stream);
        stream.start()
    }

    /// Provide a new established L2CAP channel to this remote peer.
    /// This function should be called whenever the remote associated with this peer opens an
    /// L2CAP channel after the first.
    fn receive_channel(&mut self, channel: zx::Socket) -> avdtp::Result<()> {
        let stream_id = self.opening.as_ref().cloned().ok_or(avdtp::Error::InvalidState)?;
        let stream = self.get_mut(&stream_id)?;
        let channel =
            fasync::Socket::from_socket(channel).or_else(|e| Err(avdtp::Error::ChannelSetup(e)))?;
        if !stream.receive_channel(channel)? {
            self.opening = None;
        }
        fx_log_info!("Transport channel connected to seid {}", stream_id);
        Ok(())
    }

    /// Takes the media stream from an opened StreamEndpoint.
    fn take_media_stream(
        &mut self,
        local_id: &StreamEndpointId,
    ) -> avdtp::Result<avdtp::MediaStream> {
        self.get_mut(local_id)?.take_transport()
    }

    /// Handle a single request event from the avdtp peer.
    async fn handle_request(&mut self, r: avdtp::Request) -> avdtp::Result<()> {
        fx_log_info!("Handling {:?} from peer..", r);
        match r {
            avdtp::Request::Discover { responder } => responder.send(&self.local.information()),
            avdtp::Request::GetCapabilities { responder, stream_id }
            | avdtp::Request::GetAllCapabilities { responder, stream_id } => {
                match self.local.get(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => responder.send(stream.capabilities()),
                }
            }
            // TOOO(2783): React correctly to sink connections.
            // For now, we reject all opens.
            avdtp::Request::Open { responder, stream_id: _ } => {
                responder.reject(avdtp::ErrorCode::NotSupportedCommand)
            }
            avdtp::Request::Close { responder, stream_id } => {
                match self.local.get_mut(&stream_id) {
                    None => responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream.release(responder, &self.peer).await,
                }
            }
            avdtp::Request::SetConfiguration {
                responder,
                local_stream_id,
                remote_stream_id,
                capabilities,
            } => {
                let stream = match self.local.get_mut(&local_stream_id) {
                    None => return responder.reject(None, avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                // TODO(BT-695): Confirm the MediaCodec parameters are OK
                match stream.configure(&remote_stream_id, capabilities) {
                    Ok(_) => responder.send(),
                    Err(e) => {
                        // Only happens when this is already configured.
                        responder.reject(None, avdtp::ErrorCode::SepInUse)?;
                        Err(e)
                    }
                }
            }
            avdtp::Request::GetConfiguration { stream_id, responder } => {
                let stream = match self.local.get_mut(&stream_id) {
                    None => return responder.reject(avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                match stream.get_configuration() {
                    Ok(c) => responder.send(&c),
                    Err(e) => {
                        // Only happens when the stream is in the wrong state
                        responder.reject(avdtp::ErrorCode::BadState)?;
                        Err(e)
                    }
                }
            }
            avdtp::Request::Reconfigure { responder, local_stream_id, capabilities } => {
                let stream = match self.local.get_mut(&local_stream_id) {
                    None => return responder.reject(None, avdtp::ErrorCode::BadAcpSeid),
                    Some(stream) => stream,
                };
                // TODO(41656): Actually tweak the codec parameters.
                match stream.reconfigure(capabilities) {
                    Ok(_) => responder.send(),
                    Err(e) => {
                        responder.reject(None, avdtp::ErrorCode::BadState)?;
                        Err(e)
                    }
                }
            }
            avdtp::Request::Start { responder, stream_ids } => {
                // TOOO(2783): React correctly to sink starts.
                // No endpoint should be able to get into this state unless it's already opened.
                for seid in stream_ids {
                    return responder.reject(&seid, avdtp::ErrorCode::NotSupportedCommand);
                }
                Ok(())
            }
            avdtp::Request::Suspend { responder, stream_ids } => {
                // TODO(39881): Support suspend and resume by the peer in source mode.
                for seid in stream_ids {
                    return responder.reject(&seid, avdtp::ErrorCode::NotSupportedCommand);
                }
                Ok(())
            }
            avdtp::Request::Abort { responder, stream_id } => {
                let stream = match self.local.get_mut(&stream_id) {
                    None => return Ok(()),
                    Some(stream) => stream,
                };
                stream.abort(None).await?;
                self.opening = self.opening.take().filter(|id| id != &stream_id);
                responder.send()
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{build_local_endpoints, SBC_SEID};

    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth::{Error, ErrorCode, Status};
    use fidl_fuchsia_bluetooth_bredr::{
        ProfileMarker, ProfileRequest, ServiceClassProfileIdentifier,
    };
    use futures::pin_mut;
    use std::convert::TryInto;
    use std::task::Poll;

    fn make_endpoint(seid: u8) -> StreamEndpoint {
        StreamEndpoint::new(seid, avdtp::MediaType::Audio, avdtp::EndpointType::Source, vec![])
            .expect("endpoint creation should succeed")
    }

    #[test]
    fn test_streams() {
        let mut streams = Streams::new();

        streams.insert(make_endpoint(1));
        streams.insert(make_endpoint(6));

        let first_id = 1_u8.try_into().expect("good id");
        let missing_id = 5_u8.try_into().expect("good id");

        assert!(streams.get(&first_id).is_some());
        assert!(streams.get(&missing_id).is_none());

        assert!(streams.get_mut(&first_id).is_some());
        assert!(streams.get_mut(&missing_id).is_none());

        let expected_info = vec![make_endpoint(1).information(), make_endpoint(6).information()];

        let infos = streams.information();

        assert_eq!(expected_info.len(), infos.len());

        if infos[0].id() == &first_id {
            assert_eq!(expected_info[0], infos[0]);
            assert_eq!(expected_info[1], infos[1]);
        } else {
            assert_eq!(expected_info[0], infos[1]);
            assert_eq!(expected_info[1], infos[0]);
        }
    }

    fn setup_avdtp_peer() -> (avdtp::Peer, zx::Socket) {
        let (remote, signaling) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("socket creation fail");

        let peer = avdtp::Peer::new(signaling).expect("create peer failure");
        (peer, remote)
    }

    pub(crate) fn recv_remote(remote: &zx::Socket) -> Result<Vec<u8>, zx::Status> {
        let waiting = remote.outstanding_read_bytes();
        assert!(waiting.is_ok());
        let mut response: Vec<u8> = vec![0; waiting.unwrap()];
        let response_read = remote.read(response.as_mut_slice())?;
        assert_eq!(response.len(), response_read);
        Ok(response)
    }

    fn expect_get_capabilities_and_respond(
        remote: &zx::Socket,
        expected_seid: u8,
        response_capabilities: &[u8],
    ) {
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x02, received[1]); // 0x02 = Get Capabilities
        assert_eq!(expected_seid << 2, received[2]);

        let txlabel_raw = received[0] & 0xF0;

        // Expect a get capabilities and respond.
        #[rustfmt::skip]
        let mut get_capabilities_rsp = vec![
            txlabel_raw << 4 | 0x2, // TxLabel (same) + ResponseAccept (0x02)
            0x02 // GetCapabilities
        ];

        get_capabilities_rsp.extend_from_slice(response_capabilities);

        assert!(remote.write(&get_capabilities_rsp).is_ok());
    }

    fn expect_get_all_capabilities_and_respond(
        remote: &zx::Socket,
        expected_seid: u8,
        response_capabilities: &[u8],
    ) {
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x0C, received[1]); // 0x0C = Get All Capabilities
        assert_eq!(expected_seid << 2, received[2]);

        let txlabel_raw = received[0] & 0xF0;

        // Expect a get capabilities and respond.
        #[rustfmt::skip]
        let mut get_capabilities_rsp = vec![
            txlabel_raw << 4 | 0x2, // TxLabel (same) + ResponseAccept (0x02)
            0x0C // GetAllCapabilities
        ];

        get_capabilities_rsp.extend_from_slice(response_capabilities);

        assert!(remote.write(&get_capabilities_rsp).is_ok());
    }

    #[test]
    fn test_peer_collect_capabilities_success() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (avdtp, remote) = setup_avdtp_peer();

        let (profile_proxy, _) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");

        let peer = Peer::create(
            PeerId(1),
            avdtp,
            build_local_endpoints().expect("endpoints"),
            profile_proxy,
        );
        let p: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        };
        peer.set_descriptor(p);

        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a discover command.
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;

        // Respond with a set of streams.
        let response: &[u8] = &[
            txlabel_raw << 4 | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            0x01,                              // Discover
            0x3E << 2 | 0x0 << 1,              // SEID (3E), Not In Use (0b0)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
            0x01 << 2 | 0x1 << 1,              // SEID (1), In Use (0b1)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
        ];
        assert!(remote.write(response).is_ok());

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get capabilities and respond.
        #[rustfmt::skip]
        let capabilities_rsp = &[
            // MediaTransport (Length of Service Capability = 0)
            0x01, 0x00,
            // Media Codec (LOSC = 2 + 4), Media Type Audio (0x00), Codec type (0x40), Codec specific 0xF09F9296
            0x07, 0x06, 0x00, 0x40, 0xF0, 0x9F, 0x92, 0x96
        ];
        expect_get_capabilities_and_respond(&remote, 0x3E, capabilities_rsp);

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get capabilities and respond.
        #[rustfmt::skip]
        let capabilities_rsp = &[
            // MediaTransport (Length of Service Capability = 0)
            0x01, 0x00,
            // Media Codec (LOSC = 2 + 2), Media Type Audio (0x00), Codec type (0x00), Codec specific 0xC0DE
            0x07, 0x04, 0x00, 0x00, 0xC0, 0xDE
        ];
        expect_get_capabilities_and_respond(&remote, 0x01, capabilities_rsp);

        // Should finish!
        let res = exec.run_until_stalled(&mut collect_future);
        assert!(res.is_ready());

        match res {
            Poll::Pending => panic!("collect capabilities should be complete"),
            Poll::Ready(Err(e)) => panic!("collect capabilities should have succeeded: {}", e),
            Poll::Ready(Ok(endpoints)) => {
                let first_seid: StreamEndpointId = 0x3E_u8.try_into().unwrap();
                let second_seid: StreamEndpointId = 0x01_u8.try_into().unwrap();
                for stream in endpoints {
                    if stream.local_id() == &first_seid {
                        let expected_caps = vec![
                            ServiceCapability::MediaTransport,
                            ServiceCapability::MediaCodec {
                                media_type: avdtp::MediaType::Audio,
                                codec_type: avdtp::MediaCodecType::new(0x40),
                                codec_extra: vec![0xF0, 0x9F, 0x92, 0x96],
                            },
                        ];
                        assert_eq!(&expected_caps, stream.capabilities());
                    } else if stream.local_id() == &second_seid {
                        let expected_codec_type = avdtp::MediaCodecType::new(0x00);
                        assert_eq!(Some(&expected_codec_type), stream.codec_type());
                    } else {
                        panic!("Unexpected endpoint in the streams collected");
                    }
                }
            }
        }
    }

    #[test]
    fn test_peer_collect_all_capabilities_success() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (avdtp, remote) = setup_avdtp_peer();

        let (profile_proxy, _) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");

        let peer = Peer::create(
            PeerId(1),
            avdtp,
            build_local_endpoints().expect("endpoints"),
            profile_proxy,
        );
        let p: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        };
        peer.set_descriptor(p);

        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a discover command.
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;

        // Respond with a set of streams.
        let response: &[u8] = &[
            txlabel_raw << 4 | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            0x01,                              // Discover
            0x3E << 2 | 0x0 << 1,              // SEID (3E), Not In Use (0b0)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
            0x01 << 2 | 0x1 << 1,              // SEID (1), In Use (0b1)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
        ];
        assert!(remote.write(response).is_ok());

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get all capabilities and respond.
        #[rustfmt::skip]
        let capabilities_rsp = &[
            // MediaTransport (Length of Service Capability = 0)
            0x01, 0x00,
            // Media Codec (LOSC = 2 + 4), Media Type Audio (0x00), Codec type (0x40), Codec specific 0xF09F9296
            0x07, 0x06, 0x00, 0x40, 0xF0, 0x9F, 0x92, 0x96
        ];
        expect_get_all_capabilities_and_respond(&remote, 0x3E, capabilities_rsp);

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get all capabilities and respond.
        #[rustfmt::skip]
        let capabilities_rsp = &[
            // MediaTransport (Length of Service Capability = 0)
            0x01, 0x00,
            // Media Codec (LOSC = 2 + 2), Media Type Audio (0x00), Codec type (0x00), Codec specific 0xC0DE
            0x07, 0x04, 0x00, 0x00, 0xC0, 0xDE
        ];
        expect_get_all_capabilities_and_respond(&remote, 0x01, capabilities_rsp);

        // Should finish!
        let res = exec.run_until_stalled(&mut collect_future);
        assert!(res.is_ready());

        match res {
            Poll::Pending => panic!("collect capabilities should be complete"),
            Poll::Ready(Err(e)) => panic!("collect capabilities should have succeeded: {}", e),
            Poll::Ready(Ok(map)) => {
                let seid = 0x3E_u8.try_into().unwrap();
                assert!(map.contains_key(&seid));
                let expected_codec = ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::new(0x40),
                    codec_extra: vec![0xF0, 0x9F, 0x92, 0x96],
                };
                assert_eq!(Some(&expected_codec), map.get(&seid));
                let seid = 0x01_u8.try_into().unwrap();
                assert!(map.contains_key(&seid));
                let expected_codec = ServiceCapability::MediaCodec {
                    media_type: avdtp::MediaType::Audio,
                    codec_type: avdtp::MediaCodecType::new(0x00),
                    codec_extra: vec![0xC0, 0xDE],
                };
                assert_eq!(Some(&expected_codec), map.get(&seid));
            }
        }
    }

    #[test]
    fn test_peer_collect_capabilities_discovery_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (avdtp, remote) = setup_avdtp_peer();

        let (profile_proxy, _) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");

        let peer = Peer::create(
            PeerId(1),
            avdtp,
            build_local_endpoints().expect("endpoints"),
            profile_proxy,
        );

        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        // Shouldn't finish yet.
        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a discover command.
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;

        // Respond with an eror.
        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x3, // txlabel (same), Single (0b00), Response Reject (0b11)
            0x01,                         // Discover
            0x31,                         // BAD_STATE
        ];
        assert!(remote.write(response).is_ok());

        // Should be done with an error.
        // Should finish!
        let res = exec.run_until_stalled(&mut collect_future);
        match res {
            Poll::Pending => panic!("Should be ready after discovery failure"),
            Poll::Ready(Ok(x)) => panic!("Should be an error but returned {:?}", x),
            Poll::Ready(Err(e)) => assert_eq!(avdtp::Error::RemoteRejected(0x31), e),
        }
    }

    #[test]
    fn test_peer_collect_capabilities_get_capability_fails() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (avdtp, remote) = setup_avdtp_peer();

        let (profile_proxy, _) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");

        let peer = Peer::create(
            PeerId(1),
            avdtp,
            build_local_endpoints().expect("endpoints"),
            profile_proxy,
        );

        let collect_future = peer.collect_capabilities();
        pin_mut!(collect_future);

        // Shouldn't finish yet.
        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a discover command.
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;

        // Respond with a set of streams.
        let response: &[u8] = &[
            txlabel_raw << 4 | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            0x01,                              // Discover
            0x3E << 2 | 0x0 << 1,              // SEID (3E), Not In Use (0b0)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
            0x01 << 2 | 0x1 << 1,              // SEID (1), In Use (0b1)
            0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
        ];
        assert!(remote.write(response).is_ok());

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get capabilities request
        let expected_seid = 0x3E;
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x02, received[1]); // 0x02 = Get Capabilities
        assert_eq!(expected_seid << 2, received[2]);

        let txlabel_raw = received[0] & 0xF0;

        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x3, // txlabel (same), Single (0b00), Response Reject (0b11)
            0x02,                         // Get Capabilities
            0x12,                         // BAD_ACP_SEID
        ];
        assert!(remote.write(response).is_ok());

        assert!(exec.run_until_stalled(&mut collect_future).is_pending());

        // Expect a get capabilities request (skipped the last one)
        let expected_seid = 0x01;
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x02, received[1]); // 0x02 = Get Capabilities
        assert_eq!(expected_seid << 2, received[2]);

        let txlabel_raw = received[0] & 0xF0;

        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x3, // txlabel (same), Single (0b00), Response Reject (0b11)
            0x02,                         // Get Capabilities
            0x12,                         // BAD_ACP_SEID
        ];
        assert!(remote.write(response).is_ok());

        // Should be done without an error, but with no streams.
        let res = exec.run_until_stalled(&mut collect_future);
        match res {
            Poll::Pending => panic!("Should be ready after discovery failure"),
            Poll::Ready(Err(e)) => panic!("Shouldn't be an error but returned {:?}", e),
            Poll::Ready(Ok(map)) => assert_eq!(0, map.len()),
        }
    }

    fn receive_simple_accept(remote: &zx::Socket, signal_id: u8) {
        let received = recv_remote(&remote).expect("expected a packet");
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(signal_id, received[1]);

        let txlabel_raw = received[0] & 0xF0;

        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            signal_id,
        ];
        assert!(remote.write(response).is_ok());
    }

    #[test]
    fn test_peer_start_stream_success() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (avdtp, remote) = setup_avdtp_peer();

        let (profile_proxy, mut profile_request_stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");

        let peer = Peer::create(
            PeerId(1),
            avdtp,
            build_local_endpoints().expect("endpoints"),
            profile_proxy,
        );

        // This needs to match the local SBC_SEID
        let local_seid = SBC_SEID.try_into().unwrap();
        let remote_seid = 2_u8.try_into().unwrap();

        let codec_params = ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: vec![0x29, 0xF5, 2, 250],
        };

        let start_future = peer.start_stream(local_seid, remote_seid, codec_params);
        pin_mut!(start_future);

        assert!(exec.run_until_stalled(&mut start_future).is_pending());

        receive_simple_accept(&remote, 0x03); // Set Configuration

        assert!(exec.run_until_stalled(&mut start_future).is_pending());

        receive_simple_accept(&remote, 0x06); // Open

        match exec.run_until_stalled(&mut start_future) {
            Poll::Pending => {}
            Poll::Ready(Err(e)) => panic!("Expected to be pending but error: {:?}", e),
            Poll::Ready(Ok(_)) => panic!("Expected to be pending but finished!"),
        };

        // Should connect the media socket after open.
        let (_, transport) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("socket creation fail");
        let request = exec.run_until_stalled(&mut profile_request_stream.next());
        match request {
            Poll::Ready(Some(Ok(ProfileRequest::ConnectL2cap { peer_id, psm: _, responder }))) => {
                assert_eq!(PeerId(1), peer_id.parse().expect("peer_id parses"));
                responder
                    .send(&mut Status { error: None }, Some(transport))
                    .expect("responder sends");
            }
            x => panic!("Should have sent a open l2cap request, but got {:?}", x),
        };

        match exec.run_until_stalled(&mut start_future) {
            Poll::Pending => {}
            Poll::Ready(Err(e)) => panic!("Expected to be pending but error: {:?}", e),
            Poll::Ready(Ok(_)) => panic!("Expected to be pending but finished!"),
        };

        receive_simple_accept(&remote, 0x07); // Start

        // Should return the media stream (which should be connected)
        // Should be done without an error, but with no streams.
        let res = exec.run_until_stalled(&mut start_future);
        match res {
            Poll::Pending => panic!("Should be ready after start succeeds"),
            Poll::Ready(Err(e)) => panic!("Shouldn't be an error but returned {:?}", e),
            // TODO: confirm the stream is usable
            Poll::Ready(Ok(_stream)) => {}
        }
    }

    #[test]
    fn test_peer_start_stream_fails_to_connect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (avdtp, remote) = setup_avdtp_peer();

        let (profile_proxy, mut profile_request_stream) =
            create_proxy_and_stream::<ProfileMarker>().expect("test proxy pair creation");

        let peer = Peer::create(
            PeerId(1),
            avdtp,
            build_local_endpoints().expect("endpoints"),
            profile_proxy,
        );

        // This needs to match the local SBC_SEID
        let local_seid = SBC_SEID.try_into().unwrap();
        let remote_seid = 2_u8.try_into().unwrap();

        let codec_params = ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: vec![0x29, 0xF5, 2, 250],
        };

        let start_future = peer.start_stream(local_seid, remote_seid, codec_params);
        pin_mut!(start_future);

        assert!(exec.run_until_stalled(&mut start_future).is_pending());

        receive_simple_accept(&remote, 0x03); // Set Configuration

        assert!(exec.run_until_stalled(&mut start_future).is_pending());

        receive_simple_accept(&remote, 0x06); // Open

        match exec.run_until_stalled(&mut start_future) {
            Poll::Pending => {}
            Poll::Ready(Err(e)) => panic!("Expected to be pending but error: {:?}", e),
            Poll::Ready(Ok(_)) => panic!("Expected to be pending but finished!"),
        };

        // Should connect the media socket after open.
        let request = exec.run_until_stalled(&mut profile_request_stream.next());
        match request {
            Poll::Ready(Some(Ok(ProfileRequest::ConnectL2cap { peer_id, psm: _, responder }))) => {
                assert_eq!(PeerId(1), peer_id.parse().expect("peer_id parses"));
                responder
                    .send(
                        &mut Status {
                            error: Some(Box::new(Error {
                                error_code: ErrorCode::Failed,
                                protocol_error_code: 0,
                                description: None,
                            })),
                        },
                        None,
                    )
                    .expect("responder sends");
            }
            x => panic!("Should have sent a open l2cap request, but got {:?}", x),
        };

        // Should return an error.
        // Should be done without an error, but with no streams.
        let res = exec.run_until_stalled(&mut start_future);
        match res {
            Poll::Pending => panic!("Should be ready after start fails"),
            Poll::Ready(Ok(_stream)) => panic!("Shouldn't have succeeded stream here"),
            Poll::Ready(Err(_)) => {}
        }
    }

    #[test]
    /// Test that the version check method correctly differentiates between newer
    /// and older A2DP versions.
    fn test_a2dp_version_check() {
        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 3,
        };
        assert_eq!(true, a2dp_version_check(p1));

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 2,
            minor_version: 10,
        };
        assert_eq!(true, a2dp_version_check(p1));

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 0,
        };
        assert_eq!(false, a2dp_version_check(p1));

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 0,
            minor_version: 9,
        };
        assert_eq!(false, a2dp_version_check(p1));

        let p1: ProfileDescriptor = ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 2,
            minor_version: 2,
        };
        assert_eq!(true, a2dp_version_check(p1));
    }
}
