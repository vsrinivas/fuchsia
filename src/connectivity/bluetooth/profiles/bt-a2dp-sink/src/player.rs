// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitfield::bitfield,
    failure::{bail, format_err, Error, ResultExt},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_media::{
        AudioSampleFormat, AudioStreamType, MediumSpecificStreamType, SimpleStreamSinkProxy,
        StreamPacket, StreamType, AUDIO_ENCODING_SBC, NO_TIMESTAMP,
        STREAM_PACKET_FLAG_DISCONTINUITY,
    },
    fidl_fuchsia_media_playback::{
        PlayerEvent, PlayerEventStream, PlayerMarker, PlayerProxy, SourceMarker,
    },
    fuchsia_zircon as zx,
    futures::{stream, StreamExt},
};

const DEFAULT_BUFFER_LEN: usize = 65536;

/// Players are configured and accept media frames, which are sent to the
/// media subsystem.
pub struct Player {
    buffer: zx::Vmo,
    buffer_len: usize,
    codec: String,
    current_offset: usize,
    stream_source: SimpleStreamSinkProxy,
    player: PlayerProxy,
    events: PlayerEventStream,
    playing: bool,
    next_packet_flags: u32,
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

impl Player {
    /// Attempt to make a new player that decodes and plays frames encoded in the
    /// `codec`
    // TODO(jamuraa): add encoding parameters for this (SetConfiguration)
    pub async fn new(codec: String) -> Result<Player, Error> {
        let player = fuchsia_component::client::connect_to_service::<PlayerMarker>()
            .context("Failed to connect to media player")?;
        let (source_client, source) = fidl::endpoints::create_endpoints()?;
        let source_proxy = source_client.into_proxy()?;
        player.create_elementary_source(0, false, false, None, source)?;

        let audio_stream_type = AudioStreamType {
            sample_format: AudioSampleFormat::Signed16,
            channels: 2,              // Stereo
            frames_per_second: 44100, // 44.1kHz
        };

        let mut stream_type = StreamType {
            medium_specific: MediumSpecificStreamType::Audio(audio_stream_type),
            encoding: codec.clone(),
            encoding_parameters: None,
        };

        let (stream_source, stream_source_sink) = fidl::endpoints::create_endpoints()?;
        let stream_source = stream_source.into_proxy()?;
        source_proxy.add_stream(&mut stream_type, 44100, 1, stream_source_sink)?;

        // TODO: vmar map this for faster access.
        let buffer =
            zx::Vmo::create_with_opts(zx::VmoOptions::NON_RESIZABLE, DEFAULT_BUFFER_LEN as u64)?;

        stream_source.add_payload_buffer(
            0,
            buffer.create_child(
                zx::VmoChildOptions::COPY_ON_WRITE,
                0,
                DEFAULT_BUFFER_LEN as u64,
            )?,
        )?;

        let mut player_event_stream = player.take_event_stream();

        let source_client_channel = source_proxy.into_channel().unwrap().into_zx_channel();
        let upcasted_source = ClientEnd::<SourceMarker>::new(source_client_channel);
        player.set_source(Some(upcasted_source))?;

        // We should be able to wait until either
        // (1) audio is connected or
        // (2) there is a Problem.
        loop {
            let x = await!(player_event_stream.next());
            if x.is_none() {
                // The player closed the event stream, something is wrong.
                return Err(format_err!("MediaPlayer closed"));
            }
            let evt = x.unwrap();
            if evt.is_err() {
                return Err(evt.unwrap_err().into());
            }
            let PlayerEvent::OnStatusChanged { player_status } = evt.unwrap();
            if let Some(problem) = player_status.problem {
                return Err(format_err!(
                    "Problem setting up: {} - {:?}",
                    problem.type_,
                    problem.details
                ));
            }
            if player_status.audio_connected {
                break;
            }
        }

        Ok(Player {
            buffer,
            buffer_len: DEFAULT_BUFFER_LEN,
            codec,
            stream_source,
            player,
            events: player_event_stream,
            current_offset: 0,
            playing: false,
            next_packet_flags: 0,
        })
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
        let mut offset = 13;
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
            } else {
                return Err(format_err!("Unrecognized codec!"));
            }
        }
        Ok(())
    }

    /// Push an encoded media frame into the buffer and signal that it's there to media.
    pub fn send_frame(&mut self, frame: &[u8]) -> Result<(), Error> {
        if frame.len() > self.buffer_len {
            self.stream_source.end_of_stream()?;
            bail!("frame is too large for buffer");
        }
        if self.current_offset + frame.len() > self.buffer_len {
            self.current_offset = 0;
        }
        self.buffer.write(frame, self.current_offset as u64)?;
        let mut packet = StreamPacket {
            pts: NO_TIMESTAMP,
            payload_buffer_id: 0,
            payload_offset: self.current_offset as u64,
            payload_size: frame.len() as u64,
            buffer_config: 0,
            flags: self.next_packet_flags,
            stream_segment_id: 0,
        };

        self.stream_source.send_packet_no_reply(&mut packet)?;

        self.current_offset += frame.len();
        self.next_packet_flags = 0;
        Ok(())
    }

    pub fn playing(&self) -> bool {
        self.playing
    }

    pub fn play(&mut self) -> Result<(), Error> {
        self.player.play()?;
        self.playing = true;
        Ok(())
    }

    pub fn pause(&mut self) -> Result<(), Error> {
        self.player.pause()?;
        self.playing = false;
        Ok(())
    }

    pub fn next_event<'a>(&'a mut self) -> stream::Next<PlayerEventStream> {
        self.events.next()
    }
}

impl Drop for Player {
    fn drop(&mut self) {
        self.pause().unwrap_or_else(|e| println!("Error in drop: {:}", e));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
