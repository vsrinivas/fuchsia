// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_a2dp::codec::MediaCodecConfig,
    bt_avdtp::{self as avdtp, ServiceCapability, StreamEndpoint, StreamEndpointId},
    fuchsia_bluetooth::types::PeerId,
    std::{collections::HashMap, convert::TryFrom, fmt, sync::Arc},
};

use crate::media_task::{MediaTask, MediaTaskBuilder};

pub struct Stream {
    endpoint: avdtp::StreamEndpoint,
    /// The builder for media tasks associated with this endpoint.
    media_task_builder: Arc<Box<dyn MediaTaskBuilder>>,
    /// The MediaTask which is currently active for this endpoint, if it is configured.
    media_task: Option<Box<dyn MediaTask>>,
    /// The peer associated with thie endpoint, if it is configured.
    /// Used during reconfiguration for MediaTask recreation.
    peer_id: Option<PeerId>,
}

impl fmt::Debug for Stream {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Stream")
            .field("endpoint", &self.endpoint)
            .field("peer_id", &self.peer_id)
            .field("has media_task", &self.media_task.is_some())
            .finish()
    }
}

impl Stream {
    pub fn build(
        endpoint: avdtp::StreamEndpoint,
        media_task_builder: impl MediaTaskBuilder + 'static,
    ) -> Self {
        Self {
            endpoint,
            media_task_builder: Arc::new(Box::new(media_task_builder)),
            media_task: None,
            peer_id: None,
        }
    }

    fn as_new(&self) -> Self {
        Self {
            endpoint: self.endpoint.as_new(),
            media_task_builder: self.media_task_builder.clone(),
            media_task: None,
            peer_id: None,
        }
    }

    pub fn endpoint(&self) -> &StreamEndpoint {
        &self.endpoint
    }

    pub fn endpoint_mut(&mut self) -> &mut StreamEndpoint {
        &mut self.endpoint
    }

    fn codec_config_from_caps(
        capabilities: &[ServiceCapability],
    ) -> avdtp::Result<MediaCodecConfig> {
        // Find the MediaCodec Capability
        capabilities
            .iter()
            .find_map(|cap| match cap {
                x @ ServiceCapability::MediaCodec { .. } => Some(MediaCodecConfig::try_from(x)),
                _ => None,
            })
            .ok_or(avdtp::Error::OutOfRange)?
    }

    pub fn configure(
        &mut self,
        peer_id: &PeerId,
        remote_id: &StreamEndpointId,
        capabilities: Vec<ServiceCapability>,
    ) -> avdtp::Result<()> {
        let codec_config = Self::codec_config_from_caps(&capabilities)?;
        if self.media_task.is_some() {
            return Err(avdtp::Error::InvalidState);
        }
        self.media_task = Some(self.media_task_builder.configure(&peer_id, &codec_config)?);
        self.peer_id = Some(peer_id.clone());
        self.endpoint.configure(remote_id, capabilities)
    }

    pub fn reconfigure(&mut self, capabilities: Vec<ServiceCapability>) -> avdtp::Result<()> {
        let codec_config = Self::codec_config_from_caps(&capabilities)?;
        let peer_id = self.peer_id.as_ref().ok_or(avdtp::Error::InvalidState)?;
        self.media_task = Some(self.media_task_builder.configure(&peer_id, &codec_config)?);
        self.endpoint.reconfigure(capabilities)
    }

    fn media_task_ref(&mut self) -> avdtp::Result<&mut Box<dyn MediaTask>> {
        self.media_task.as_mut().ok_or(avdtp::Error::InvalidState)
    }

    /// Attempt to start the endpoint.  If the endpoint is successfully started, the media task is
    /// started.
    pub fn start(&mut self) -> avdtp::Result<()> {
        if self.media_task.is_none() {
            return Err(avdtp::Error::InvalidState);
        }
        let transport = self.endpoint.take_transport()?;
        let _ = self.endpoint.start()?;
        self.media_task_ref()?.start(transport).map_err(Into::into)
    }

    /// Suspends the media processor and endpoint.
    pub fn suspend(&mut self) -> avdtp::Result<()> {
        self.endpoint.suspend()?;
        self.media_task_ref()?.stop().map_err(Into::into)
    }

    /// Releases the endpoint and stops the processing of audio.
    pub async fn release(
        &mut self,
        responder: avdtp::SimpleResponder,
        peer: &avdtp::Peer,
    ) -> avdtp::Result<()> {
        self.media_task.take().map(|mut x| x.stop());
        self.peer_id = None;
        self.endpoint.release(responder, peer).await
    }

    pub async fn abort(&mut self, peer: Option<&avdtp::Peer>) -> avdtp::Result<()> {
        self.media_task.take().map(|mut x| x.stop());
        self.peer_id = None;
        self.endpoint.abort(peer).await
    }
}

pub struct Streams(HashMap<StreamEndpointId, Stream>);

impl Streams {
    /// A new empty set of streams.
    pub fn new() -> Self {
        Self(HashMap::new())
    }

    /// Makes a copy of this set of streams, but with all streams copied with their states set to
    /// idle.
    pub fn as_new(&self) -> Self {
        let mut new_map = HashMap::new();
        for (id, stream) in self.0.iter() {
            new_map.insert(id.clone(), stream.as_new());
        }
        Self(new_map)
    }

    /// Inserts a stream, indexing it by the local endpoint id.
    /// It replaces any other stream with the same endpoint id.
    pub fn insert(&mut self, stream: Stream) {
        self.0.insert(stream.endpoint().local_id().clone(), stream);
    }

    /// Retrieves a reference to the Stream referenced by `id`, if the stream exists,
    pub fn get(&mut self, id: &StreamEndpointId) -> Option<&Stream> {
        self.0.get(id)
    }

    /// Retrieves a mutable reference to the Stream referenced by `id`, if the stream exists,
    pub fn get_mut(&mut self, id: &StreamEndpointId) -> Option<&mut Stream> {
        self.0.get_mut(id)
    }

    /// Returns a vector of information on all the contained streams.
    pub fn information(&self) -> Vec<avdtp::StreamInformation> {
        self.0.values().map(|x| x.endpoint().information()).collect()
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    use crate::media_task::tests::TestMediaTaskBuilder;

    use bt_a2dp::media_types::*;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::pin_mut;
    use std::convert::TryInto;
    use std::task::Poll;

    pub fn build_test_streams() -> Streams {
        let mut streams = Streams::new();
        streams.insert(make_stream(1));
        streams
    }

    fn sbc_mediacodec_capability() -> avdtp::ServiceCapability {
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ48000HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::MANDATORY_SRC,
            SbcSubBands::MANDATORY_SRC,
            SbcAllocation::MANDATORY_SRC,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )
        .expect("SBC codec info");

        ServiceCapability::MediaCodec {
            media_type: avdtp::MediaType::Audio,
            codec_type: avdtp::MediaCodecType::AUDIO_SBC,
            codec_extra: sbc_codec_info.to_bytes().to_vec(),
        }
    }

    pub fn make_sbc_endpoint(seid: u8) -> StreamEndpoint {
        StreamEndpoint::new(
            seid,
            avdtp::MediaType::Audio,
            avdtp::EndpointType::Source,
            vec![avdtp::ServiceCapability::MediaTransport, sbc_mediacodec_capability()],
        )
        .expect("endpoint creation should succeed")
    }

    fn make_stream(seid: u8) -> Stream {
        Stream::build(make_sbc_endpoint(seid), TestMediaTaskBuilder::new().builder())
    }

    #[test]
    fn test_streams() {
        let mut streams = Streams::new();

        streams.insert(make_stream(1));
        streams.insert(make_stream(6));

        let first_id = 1_u8.try_into().expect("good id");
        let missing_id = 5_u8.try_into().expect("good id");

        assert!(streams.get(&first_id).is_some());
        assert!(streams.get(&missing_id).is_none());

        assert!(streams.get_mut(&first_id).is_some());
        assert!(streams.get_mut(&missing_id).is_none());

        let expected_info =
            vec![make_sbc_endpoint(1).information(), make_sbc_endpoint(6).information()];

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

    #[test]
    fn test_suspend_stops_media_task() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let mut task_builder = TestMediaTaskBuilder::new();
        let mut stream = Stream::build(make_sbc_endpoint(1), task_builder.builder());
        let next_task_fut = task_builder.next_task();
        let remote_id = 1_u8.try_into().expect("good id");

        let sbc_codec_cap = sbc_mediacodec_capability();
        let expected_codec_config =
            MediaCodecConfig::try_from(&sbc_codec_cap).expect("codec config");

        assert!(stream.configure(&PeerId(1), &remote_id, vec![]).is_err());
        assert!(stream.configure(&PeerId(1), &remote_id, vec![sbc_codec_cap]).is_ok());

        pin_mut!(next_task_fut);
        let task = match exec.run_until_stalled(&mut next_task_fut) {
            Poll::Ready(Some(task)) => task,
            x => panic!("Expected next task to be sent during configure, got {:?}", x),
        };

        assert_eq!(task.peer_id, PeerId(1));
        assert_eq!(task.codec_config, expected_codec_config);

        stream.endpoint_mut().establish().expect("establishment should start okay");
        let (_remote, transport) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).expect("socket creation fail");
        let transport = fasync::Socket::from_socket(transport).expect("async socket");
        stream.endpoint_mut().receive_channel(transport).expect("should be ready for a channel");

        match stream.start() {
            Ok(()) => {}
            x => panic!("Expected OK but got {:?}", x),
        };
        assert!(task.is_started());
        assert!(stream.suspend().is_ok());
        assert!(!task.is_started());
        assert!(stream.start().is_ok());
        assert!(task.is_started());
    }
}
