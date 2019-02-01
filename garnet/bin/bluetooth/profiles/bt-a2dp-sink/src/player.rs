// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitfield::bitfield,
    failure::{bail, format_err, Error, ResultExt},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_media::{
        AudioSampleFormat, AudioStreamType, MediumSpecificStreamType, SimpleStreamSinkProxy,
        StreamPacket, StreamType, AUDIO_ENCODING_SBC,
    },
    fidl_fuchsia_mediaplayer::{
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
}

bitfield! {
    pub struct SbcHeader(u32);
    impl Debug;
    u8;
    syncword, _: 7, 0;
    sampling_frequency, _: 9, 8;
    blocks, _: 11, 10;
    channel_mode, _: 13, 12;
    allocation_method, _: 14;
    subbands, _: 15;
    bitpool, _: 23, 16;
    crccheck, _: 31, 24;
}

impl Player {
    /// Attempt to make a new player that decodes and plays frames encoded in the
    /// `codec`
    // TODO(jamuraa): add encoding parameters for this (SetConfiguration)
    pub async fn new(codec: String) -> Result<Player, Error> {
        let player = fuchsia_app::client::connect_to_service::<PlayerMarker>()
            .context("Failed to connect to media player")?;
        let (source_client, source) = fidl::endpoints::create_endpoints()?;
        let source_proxy = source_client.into_proxy()?;
        player.create_stream_source(0, false, false, None, source)?;

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

        stream_source.add_payload_buffer(0, buffer.clone(0, DEFAULT_BUFFER_LEN as u64)?)?;

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
        const SBC_SYNCWORD: u8 = 0x9c;
        if buf[0] != SBC_SYNCWORD {
            return Err(format_err!("SBC frame syncword not found"));
        }
        if buf.len() < 4 {
            return Err(format_err!("Buffer too short for header"));
        }
        let head = SbcHeader(Player::as_u32_le(&buf[0..4]));
        let subbands: usize = match head.subbands() {
            false => 4,
            true => 8,
        };
        let channels: usize = match head.channel_mode() {
            0 => 1,
            _ => 2,
        };
        let blocks: usize = (head.blocks() as usize + 1) * 4;
        let bitpool: usize = head.bitpool() as usize;
        let joint = 1; // should be 0 if stereo instead
        let stereo_frame_length: usize = 4
            + (4 * subbands * channels as usize) / 8 as usize
            + ((joint * subbands + blocks * bitpool) as f64 / 8.0).ceil() as usize;
        Ok(stereo_frame_length)
    }

    /// Accepts a payload which may contain multiple frames and breaks it into
    /// frames and sends it to media.
    pub fn push_payload(&mut self, payload: &[u8]) -> Result<(), Error> {
        let mut offset = 13;
        while offset < payload.len() {
            if self.codec == AUDIO_ENCODING_SBC {
                let len = Player::find_sbc_frame_len(&payload[offset..])?;
                if offset + len > payload.len() {
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
            pts: self.current_offset as i64,
            payload_buffer_id: 0,
            payload_offset: self.current_offset as u64,
            payload_size: frame.len() as u64,
            buffer_config: 0,
            flags: 0,
            stream_id: 0,
        };

        self.stream_source.send_packet_no_reply(&mut packet)?;

        self.current_offset += frame.len();
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
        self.pause()
            .unwrap_or_else(|e| println!("Error in drop: {:}", e));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_frame_length() {
        assert_eq!(
            119,
            Player::find_sbc_frame_len(&[156, 189, 53, 102]).unwrap()
        );
    }

    #[test]
    #[should_panic(expected = "should have paniced on not enough data")]
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
