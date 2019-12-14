// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitfield::bitfield,
    bt_avdtp::RtpHeader,
    failure::{bail, format_err, Error, ResultExt},
    fidl::client::QueryResponseFut,
    fidl_fuchsia_media::{
        AudioConsumerProxy, AudioConsumerStartFlags, AudioConsumerStatus, AudioSampleFormat,
        AudioStreamType, Compression, SessionAudioConsumerFactoryMarker,
        SessionAudioConsumerFactoryProxy, StreamPacket, StreamSinkProxy, AUDIO_ENCODING_AACLATM,
        AUDIO_ENCODING_SBC, NO_TIMESTAMP, STREAM_PACKET_FLAG_DISCONTINUITY,
    },
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{
        future::Future,
        ready,
        stream::{FusedStream, Stream},
        task::{Context, Poll},
        FutureExt, StreamExt,
    },
    std::pin::Pin,
};

const DEFAULT_BUFFER_LEN: usize = 65536;

/// Players are configured and accept media frames, which are sent to the
/// media subsystem.
pub struct Player {
    buffer: zx::Vmo,
    buffer_len: usize,
    codec: String,
    current_offset: usize,
    stream_sink: StreamSinkProxy,
    audio_consumer: AudioConsumerProxy,
    watch_status_stream: HangingGetStream<
        QueryResponseFut<AudioConsumerStatus>,
        Result<AudioConsumerStatus, fidl::Error>,
    >,
    playing: bool,
    next_packet_flags: u32,
    last_seq_played: u16,
}

pub enum PlayerEvent {
    Closed,
    Status(AudioConsumerStatus),
}

#[derive(Debug, PartialEq)]
enum ChannelMode {
    Mono,
    DualChannel,
    Stereo,
    JointStereo,
}

impl From<u8> for ChannelMode {
    fn from(bits: u8) -> Self {
        match bits {
            0 => ChannelMode::Mono,
            1 => ChannelMode::DualChannel,
            2 => ChannelMode::Stereo,
            3 => ChannelMode::JointStereo,
            _ => panic!("invalid channel mode"),
        }
    }
}

bitfield! {
    pub struct SbcHeader(u32);
    impl Debug;
    u8;
    syncword, _: 7, 0;
    subbands, _: 8;
    allocation_method, _: 9;
    into ChannelMode, channel_mode, _: 11, 10;
    blocks_bits, _: 13, 12;
    frequency_bits, _: 15, 14;
    bitpool_bits, _: 23, 16;
    crccheck, _: 31, 24;
}

impl SbcHeader {
    /// The number of channels, based on the channel mode in the header.
    /// From Table 12.18 in the A2DP Spec.
    fn channels(&self) -> usize {
        match self.channel_mode() {
            ChannelMode::Mono => 1,
            _ => 2,
        }
    }

    fn has_syncword(&self) -> bool {
        const SBC_SYNCWORD: u8 = 0x9c;
        self.syncword() == SBC_SYNCWORD
    }

    /// The number of blocks, based on tbe bits in the header.
    /// From Table 12.17 in the A2DP Spec.
    fn blocks(&self) -> usize {
        4 * (self.blocks_bits() + 1) as usize
    }

    fn bitpool(&self) -> usize {
        self.bitpool_bits() as usize
    }

    /// Number of subbands based on the header bit.
    /// From Table 12.20 in the A2DP Spec.
    fn num_subbands(&self) -> usize {
        if self.subbands() {
            8
        } else {
            4
        }
    }

    /// Calculates the frame length.
    /// Formula from Section 12.9 of the A2DP Spec.
    fn frame_length(&self) -> Result<usize, Error> {
        if !self.has_syncword() {
            return Err(format_err!("syncword does not match"));
        }
        let len = 4 + (4 * self.num_subbands() * self.channels()) / 8;
        let rest = (match self.channel_mode() {
            ChannelMode::Mono | ChannelMode::DualChannel => {
                self.blocks() * self.channels() * self.bitpool()
            }
            ChannelMode::Stereo => self.blocks() * self.bitpool(),
            ChannelMode::JointStereo => self.num_subbands() + (self.blocks() * self.bitpool()),
        } as f64
            / 8.0)
            .ceil() as usize;
        Ok(len + rest)
    }
}

struct HangingGetStream<Fut, T>
where
    Fut: Future<Output = T> + Unpin,
{
    generator: Box<dyn Fn() -> Fut>,
    future: Fut,
}

impl<Fut, T> HangingGetStream<Fut, T>
where
    Fut: Future<Output = T> + Unpin,
{
    fn new(generator: Box<dyn Fn() -> Fut>) -> Self {
        let future = generator();
        Self { generator, future }
    }
}

impl<Fut, T> FusedStream for HangingGetStream<Fut, T>
where
    Fut: Future<Output = T> + Unpin,
{
    fn is_terminated(&self) -> bool {
        false
    }
}

impl<Fut, T> Stream for HangingGetStream<Fut, T>
where
    Fut: Future<Output = T> + Unpin,
{
    type Item = T;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let result = ready!(self.future.poll_unpin(cx));
        self.future = (self.generator)();
        Poll::Ready(Some(result))
    }
}

impl Player {
    /// Attempt to make a new player that decodes and plays frames encoded in the
    /// `codec`
    pub async fn new(session_id: u64, codec: String, sample_freq: u32) -> Result<Player, Error> {
        let audio_consumer_factory =
            fuchsia_component::client::connect_to_service::<SessionAudioConsumerFactoryMarker>()
                .context("Failed to connect to audio consumer factory")?;
        Self::from_proxy(session_id, codec, sample_freq, audio_consumer_factory).await
    }

    /// Build a AudioConsumer given a SessionAudioConsumerFactoryProxy.
    /// Used in tests.
    async fn from_proxy(
        session_id: u64,
        codec: String,
        sample_freq: u32,
        audio_consumer_factory: SessionAudioConsumerFactoryProxy,
    ) -> Result<Player, Error> {
        let (audio_consumer, audio_consumer_server) = fidl::endpoints::create_proxy()?;

        audio_consumer_factory.create_audio_consumer(session_id, audio_consumer_server)?;

        let (stream_sink, stream_sink_server) = fidl::endpoints::create_proxy()?;

        let mut audio_stream_type = AudioStreamType {
            sample_format: AudioSampleFormat::Signed16,
            channels: 2, // Stereo
            frames_per_second: sample_freq,
        };

        let mut compression = Compression { type_: codec.clone(), parameters: None };

        let buffer = zx::Vmo::create(DEFAULT_BUFFER_LEN as u64)?;
        let buffers = vec![buffer.duplicate_handle(zx::Rights::SAME_RIGHTS)?];

        audio_consumer.create_stream_sink(
            &mut buffers.into_iter(),
            &mut audio_stream_type,
            Some(&mut compression),
            stream_sink_server,
        )?;

        let audio_consumer_clone = audio_consumer.clone();
        let watch_status_stream =
            HangingGetStream::new(Box::new(move || audio_consumer_clone.watch_status()));

        let mut player = Player {
            buffer,
            buffer_len: DEFAULT_BUFFER_LEN,
            codec,
            stream_sink,
            audio_consumer,
            watch_status_stream,
            current_offset: 0,
            playing: false,
            next_packet_flags: 0,
            last_seq_played: 0,
        };

        // wait for initial event
        let evt = player.next_event().await;
        match evt {
            PlayerEvent::Closed => return Err(format_err!("AudioConsumer closed")),
            PlayerEvent::Status(_status) => (),
        };

        Ok(player)
    }

    /// Interpret the first four octets of the slice in `bytes` as a little-endian  u32
    /// Panics if the slice is not at least four octets.
    fn as_u32_le(bytes: &[u8]) -> u32 {
        ((bytes[3] as u32) << 24)
            + ((bytes[2] as u32) << 16)
            + ((bytes[1] as u32) << 8)
            + ((bytes[0] as u32) << 0)
    }

    /// Given a buffer with an SBC frame at the start, find the length of the
    /// SBC frame.
    fn find_sbc_frame_len(buf: &[u8]) -> Result<usize, Error> {
        if buf.len() < 4 {
            return Err(format_err!("Buffer too short for header"));
        }
        SbcHeader(Player::as_u32_le(&buf[0..4])).frame_length()
    }

    /// Accepts a payload which may contain multiple frames and breaks it into
    /// frames and sends it to media.
    pub fn push_payload(&mut self, payload: &[u8]) -> Result<(), Error> {
        let rtp = RtpHeader::new(payload)?;

        let seq = rtp.sequence_number();
        let discontinuity = seq.wrapping_sub(self.last_seq_played.wrapping_add(1));

        self.last_seq_played = seq;

        if discontinuity > 0 && self.playing() {
            self.next_packet_flags |= STREAM_PACKET_FLAG_DISCONTINUITY;
        };

        let mut offset = RtpHeader::LENGTH;

        if self.codec == AUDIO_ENCODING_SBC {
            // TODO(40918) Handle SBC packet header
            offset += 1;
        }

        while offset < payload.len() {
            if self.codec == AUDIO_ENCODING_SBC {
                let len = Player::find_sbc_frame_len(&payload[offset..]).or_else(|e| {
                    self.next_packet_flags |= STREAM_PACKET_FLAG_DISCONTINUITY;
                    Err(e)
                })?;
                if offset + len > payload.len() {
                    self.next_packet_flags |= STREAM_PACKET_FLAG_DISCONTINUITY;
                    return Err(format_err!("Ran out of buffer for SBC frame"));
                }
                self.send_frame(&payload[offset..offset + len])?;
                offset += len;
            } else if self.codec == AUDIO_ENCODING_AACLATM {
                self.send_frame(&payload[offset..])?;
                offset = payload.len();
            } else {
                return Err(format_err!("Unrecognized codec!"));
            }
        }
        Ok(())
    }

    /// Push an encoded media frame into the buffer and signal that it's there to media.
    pub fn send_frame(&mut self, frame: &[u8]) -> Result<(), Error> {
        let mut full_frame_len = frame.len();

        if self.codec == AUDIO_ENCODING_AACLATM {
            full_frame_len += 3;
        }

        if full_frame_len > self.buffer_len {
            self.stream_sink.end_of_stream()?;
            bail!("frame is too large for buffer");
        }
        if self.current_offset + full_frame_len > self.buffer_len {
            self.current_offset = 0;
        }

        let start_offset = self.current_offset;

        if self.codec == AUDIO_ENCODING_AACLATM {
            // Prepend LOAS sync word and mux length so mediaplayer decoder can handle it
            let loas_syncword = [
                0x56_u8,
                0xe0_u8 | ((frame.len() >> 8) as u8 & 0xff_u8),
                frame.len() as u8 & 0xff_u8,
            ];

            self.buffer.write(&loas_syncword, self.current_offset as u64)?;
            self.current_offset += loas_syncword.len();
        }

        self.buffer.write(frame, self.current_offset as u64)?;
        let mut packet = StreamPacket {
            pts: NO_TIMESTAMP,
            payload_buffer_id: 0,
            payload_offset: start_offset as u64,
            payload_size: full_frame_len as u64,
            buffer_config: 0,
            flags: self.next_packet_flags,
            stream_segment_id: 0,
        };

        self.stream_sink.send_packet_no_reply(&mut packet)?;

        self.current_offset += frame.len();
        self.next_packet_flags = 0;
        Ok(())
    }

    pub fn playing(&self) -> bool {
        self.playing
    }

    pub fn play(&mut self) -> Result<(), Error> {
        self.audio_consumer.start(AudioConsumerStartFlags::SupplyDriven, 0, NO_TIMESTAMP)?;
        self.playing = true;
        Ok(())
    }

    pub fn stop(&mut self) -> Result<(), Error> {
        self.audio_consumer.stop()?;
        self.playing = false;
        Ok(())
    }

    /// If PlayerEvent::Closed is returned, that indicates the underlying
    /// service went away and the player should be closed/rebuilt
    pub async fn next_event(&mut self) -> PlayerEvent {
        match self.watch_status_stream.next().await {
            None => PlayerEvent::Closed,
            Some(Err(_)) => PlayerEvent::Closed,
            Some(Ok(s)) => PlayerEvent::Status(s),
        }
    }
}

impl Drop for Player {
    fn drop(&mut self) {
        self.stop().unwrap_or_else(|e| println!("Error in drop: {:}", e));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    use {
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_media::{
            AudioConsumerRequest, AudioConsumerRequestStream, SessionAudioConsumerFactoryRequest,
            StreamSinkRequest, StreamSinkRequestStream,
        },
        fuchsia_async as fasync,
        std::task::Poll,
    };

    #[test]
    fn test_frame_length() {
        // 44.1, 16 blocks, Joint Stereo, Loudness, 8 subbands, 53 bitpool (Android P)
        let header1 = [0x9c, 0xBD, 0x35, 0xA2];
        const HEADER1_FRAMELEN: usize = 119;
        let head = SbcHeader(Player::as_u32_le(&header1));
        assert!(head.has_syncword());
        assert_eq!(16, head.blocks());
        assert_eq!(ChannelMode::JointStereo, head.channel_mode());
        assert_eq!(2, head.channels());
        assert_eq!(53, head.bitpool());
        assert_eq!(HEADER1_FRAMELEN, head.frame_length().unwrap());
        assert_eq!(
            HEADER1_FRAMELEN,
            Player::find_sbc_frame_len(&[0x9c, 0xBD, 0x35, 0xA2]).unwrap()
        );

        // 44.1, 16 blocks, Stereo, Loudness, 8 subbands, 53 bitpool (OS X)
        let header2 = [0x9c, 0xB9, 0x35, 0xA2];
        const HEADER2_FRAMELEN: usize = 118;
        let head = SbcHeader(Player::as_u32_le(&header2));
        assert!(head.has_syncword());
        assert_eq!(16, head.blocks());
        assert_eq!(ChannelMode::Stereo, head.channel_mode());
        assert_eq!(2, head.channels());
        assert_eq!(53, head.bitpool());
        assert_eq!(HEADER2_FRAMELEN, head.frame_length().unwrap());
        assert_eq!(HEADER2_FRAMELEN, Player::find_sbc_frame_len(&header2).unwrap());
    }

    /// Runs through the setup sequence of a AudioConsumer, returning the audio consumer,
    /// StreamSinkRequestStream and AudioConsumerRequestStream that it is communicating with, and
    /// the VMO payload buffer that was provided to the AudioConsumer.
    fn setup_player(
        exec: &mut fasync::Executor,
        codec: &str,
    ) -> (Player, StreamSinkRequestStream, AudioConsumerRequestStream, zx::Vmo) {
        const TEST_SAMPLE_FREQ: u32 = 48000;
        const TEST_SESSION_ID: u64 = 1;

        let (audio_consumer_factory_proxy, mut audio_consumer_factory_request_stream) =
            create_proxy_and_stream::<SessionAudioConsumerFactoryMarker>()
                .expect("proxy pair creation");

        let mut player_new_fut = Box::pin(Player::from_proxy(
            TEST_SESSION_ID,
            codec.to_string(),
            TEST_SAMPLE_FREQ,
            audio_consumer_factory_proxy,
        ));

        // player creation is done in stages, waiting for the below source/sink
        // objects to be created. Just run the creation up until the first
        // blocking point.
        assert!(exec.run_until_stalled(&mut player_new_fut).is_pending());

        let complete =
            exec.run_until_stalled(&mut audio_consumer_factory_request_stream.select_next_some());
        let audio_consumer_create_req = match complete {
            Poll::Ready(Ok(req)) => req,
            x => panic!("expected audio consumer create request message but got {:?}", x),
        };

        let (audio_consumer_create_request, session_id) = match audio_consumer_create_req {
            SessionAudioConsumerFactoryRequest::CreateAudioConsumer {
                audio_consumer_request,
                session_id,
                ..
            } => (audio_consumer_request, session_id),
        };

        assert_eq!(session_id, TEST_SESSION_ID);

        let mut audio_consumer_request_stream = audio_consumer_create_request
            .into_stream()
            .expect("a audio consumer stream to be created from the request");

        let complete =
            exec.run_until_stalled(&mut audio_consumer_request_stream.select_next_some());
        let audio_consumer_req = match complete {
            Poll::Ready(Ok(req)) => req,
            x => panic!("expected audio consumer request message but got {:?}", x),
        };

        let (stream_sink_request, mut buffers) = match audio_consumer_req {
            AudioConsumerRequest::CreateStreamSink { stream_sink_request, buffers, .. } => {
                (stream_sink_request, buffers)
            }
            _ => panic!("should be CreateElementarySource"),
        };

        let sink_vmo = buffers.remove(0);

        let sink_request_stream = stream_sink_request
            .into_stream()
            .expect("a sink request stream to be created from the request");

        let complete =
            exec.run_until_stalled(&mut audio_consumer_request_stream.select_next_some());
        let audio_consumer_req = match complete {
            Poll::Ready(Ok(req)) => req,
            x => panic!("expected audio consumer request message but got {:?}", x),
        };

        let watch_status_responder = match audio_consumer_req {
            AudioConsumerRequest::WatchStatus { responder, .. } => responder,
            _ => panic!("should be WatchStatus"),
        };

        let status = AudioConsumerStatus {
            min_lead_time: Some(50),
            max_lead_time: Some(500),
            error: None,
            presentation_timeline: None,
        };
        watch_status_responder.send(status).expect("watch status sent");

        let player = match exec.run_until_stalled(&mut player_new_fut) {
            Poll::Ready(Ok(player)) => player,
            _ => panic!("player should be done"),
        };

        (player, sink_request_stream, audio_consumer_request_stream, sink_vmo)
    }

    #[test]
    fn test_player_setup() {
        let mut exec = fasync::Executor::new().expect("executor should build");

        setup_player(&mut exec, "test");
    }

    #[test]
    /// Tests that the creation of a player executes with the expected interaction with the
    /// AudioConsumer and stream setup.
    /// This tests that the buffer is sent correctly and that data "sent" through the shared
    /// VMO is readable by the receiver of the VMO.
    /// We do this by mocking the AudioConsumer and StreamSink interfaces that are used.
    fn test_send_frame() {
        let mut exec = fasync::Executor::new().expect("executor should build");

        let (mut player, mut sink_request_stream, _, sink_vmo) = setup_player(&mut exec, "test");

        let payload = &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

        player.send_frame(payload).expect("send happens okay");

        let complete = exec.run_until_stalled(&mut sink_request_stream.select_next_some());
        let sink_req = match complete {
            Poll::Ready(Ok(req)) => req,
            x => panic!("expected sink request message but got {:?}", x),
        };

        let (offset, size) = match sink_req {
            StreamSinkRequest::SendPacketNoReply { packet, .. } => {
                (packet.payload_offset, packet.payload_size as usize)
            }
            _ => panic!("should have received a packet"),
        };

        let mut recv = Vec::with_capacity(size);
        recv.resize(size, 0);

        sink_vmo.read(recv.as_mut_slice(), offset).expect("should be able to read packet data");

        assert_eq!(recv, payload, "received didn't match payload");
    }

    /// Helper function for pushing payloads to player and returning the packet flags
    fn push_payload_get_flags(
        payload: &[u8],
        exec: &mut fasync::Executor,
        player: &mut Player,
        sink_request_stream: &mut StreamSinkRequestStream,
    ) -> u32 {
        player.push_payload(payload).expect("send happens okay");

        let complete = exec.run_until_stalled(&mut sink_request_stream.select_next_some());
        let sink_req = match complete {
            Poll::Ready(Ok(req)) => req,
            x => panic!("expected player req message but got {:?}", x),
        };

        match sink_req {
            StreamSinkRequest::SendPacketNoReply { packet, .. } => packet.flags,
            _ => panic!("should have received a packet"),
        }
    }

    #[test]
    /// Test that discontinuous packets are flagged as such. We do this by
    /// sending packets through a Player and examining them after they come out
    /// of the mock StreamSink interface.
    fn test_packet_discontinuities() {
        let mut exec = fasync::Executor::new().expect("executor should build");

        let (mut player, mut sink_request_stream, mut player_request_stream, _) =
            setup_player(&mut exec, AUDIO_ENCODING_AACLATM);

        let complete = exec.run_until_stalled(&mut player_request_stream.select_next_some());
        let player_req = match complete {
            Poll::Ready(Ok(req)) => req,
            x => panic!("expected player req message but got {:?}", x),
        };

        assert_matches!(player_req, AudioConsumerRequest::WatchStatus { .. });

        player.play().expect("player plays");

        let complete = exec.run_until_stalled(&mut player_request_stream.select_next_some());
        let player_req = match complete {
            Poll::Ready(Ok(req)) => req,
            x => panic!("expected player req message but got {:?}", x),
        };

        assert_matches!(player_req, AudioConsumerRequest::Start { .. });

        // raw rtp header with sequence number of 1
        let mut raw = [128, 96, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 5];

        let flags = push_payload_get_flags(&raw, &mut exec, &mut player, &mut sink_request_stream);
        // should not have a discontinuity yet
        assert_eq!(flags & STREAM_PACKET_FLAG_DISCONTINUITY, 0);

        // increment sequence number
        raw[3] = 2;
        let flags = push_payload_get_flags(&raw, &mut exec, &mut player, &mut sink_request_stream);
        // should not have a discontinuity yet
        assert_eq!(flags & STREAM_PACKET_FLAG_DISCONTINUITY, 0);

        // introduce discont
        raw[3] = 8;
        let flags = push_payload_get_flags(&raw, &mut exec, &mut player, &mut sink_request_stream);
        assert_eq!(flags & STREAM_PACKET_FLAG_DISCONTINUITY, STREAM_PACKET_FLAG_DISCONTINUITY);
    }

    #[test]
    #[should_panic(expected = "out of bounds")]
    fn test_as_u32_le_len() {
        let _ = Player::as_u32_le(&[0, 1, 2]);
    }

    #[test]
    fn test_as_u32_le() {
        assert_eq!(1, Player::as_u32_le(&[1, 0, 0, 0]));
        assert_eq!(0xff00ff00, Player::as_u32_le(&[0, 0xff, 0, 0xff]));
        assert_eq!(0xffffffff, Player::as_u32_le(&[0xff, 0xff, 0xff, 0xff]));
    }
}
