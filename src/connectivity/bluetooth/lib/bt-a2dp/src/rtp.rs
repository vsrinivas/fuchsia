// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bitfield::bitfield,
    std::{cmp::min, collections::VecDeque},
};

bitfield! {
    /// The header for an RTP packet.
    /// The meanding of each field will vary based on the negotiated codec.
    struct RtpHeader(MSB0 [u8]);
    u8, version, set_version: 1, 0;
    bool, padding, set_padding: 2;
    bool, extension, set_extension: 3;
    u8, csrc_count, set_csrc_count: 7, 4;
    bool, marker, set_marker: 8;
    u8, payload_type, set_payload_type: 15, 9;
    u16, sequence_number, set_sequence_number: 31, 16;
    u32, timestamp, set_timestamp: 63, 32;
    u32, ssrc, set_ssrc: 95, 64;
}

const RTP_HEADER_LEN: usize = 12;

/// Set to the RTP version specified by RFC 3550 which is the version supported here.
const RTP_VERSION: u8 = 2;

/// Dynamic payload type indicated by RFC 3551, recommended by the A2DP spec.
const RTP_PAYLOAD_DYNAMIC: u8 = 96;

pub trait RtpPacketBuilder: Send {
    /// Adds a `frame` of audio to the RTP packet, which represents `samples` audio samples.
    /// Returns a vector of packets that are ready to be transmitted (which can be empty),
    /// or an Error if the frame could not be added.
    fn add_frame(&mut self, frame: Vec<u8>, samples: u32) -> Result<Vec<Vec<u8>>, Error>;
}

/// An RtpPacketBuilder that will only pack whole frames, has a maximum number of frames per
/// packet, and will add an arbitrary header to the frame data after the RTP header.
// TODO(52616): deprecate this so we can fragment AAC packets.
pub(crate) struct FrameRtpPacketBuilder {
    /// The maximum number of frames to be included in each packet
    max_frames_per_packet: u8,
    /// The maximum size of a packet (usually the max TX SDU)
    max_packet_size: usize,
    /// The next packet's sequence number
    next_sequence_number: u16,
    /// Timestamp of the end of the packets sent so far. This is currently in audio sample units.
    timestamp: u32,
    /// Frames that will be in the next RtpPacket to be sent.
    frames: VecDeque<Vec<u8>>,
    /// Time that those frames represent, in the same units as `timestamp`
    frame_samples: VecDeque<u32>,
    /// Extra header to include in each packet before `frames` are added
    frame_header: Vec<u8>,
}

impl FrameRtpPacketBuilder {
    /// Make a new builder that will vend a packet every `frames_per_packet` frames. `frame_header`
    /// are header bytes added to each packet before frames are added.
    pub fn new(max_frames_per_packet: u8, max_packet_size: usize, frame_header: Vec<u8>) -> Self {
        Self {
            max_frames_per_packet,
            next_sequence_number: 1,
            timestamp: 0,
            frames: VecDeque::with_capacity(max_frames_per_packet.into()),
            frame_samples: VecDeque::with_capacity(max_frames_per_packet.into()),
            max_packet_size,
            frame_header,
        }
    }

    fn header_size(&self) -> usize {
        self.frame_header.len() + RTP_HEADER_LEN
    }

    fn pending_total_size(&self) -> usize {
        self.frames.iter().map(Vec::len).sum::<usize>() + self.header_size()
    }
}

impl RtpPacketBuilder for FrameRtpPacketBuilder {
    /// Add a frame that represents `samples` pcm audio samples into the builder.
    /// Returns one serialized RTP packet if this frame fills a packet.
    /// An error is returned if `frame` will never fit into a single packet.
    fn add_frame(&mut self, frame: Vec<u8>, samples: u32) -> Result<Vec<Vec<u8>>, Error> {
        if (frame.len() + self.header_size()) > self.max_packet_size {
            return Err(format_err!("Media packet too large for RTP max size"));
        }
        self.frames.push_back(frame);
        self.frame_samples.push_back(samples);
        // IF Some(idx), a packet should be produced using the frames 0..idx.
        let packet_ready_idx = if self.pending_total_size() > self.max_packet_size {
            // The newest frame put us over the limit, so
            self.frames.len() - 1
        } else if self.frames.len() >= self.max_frames_per_packet.into() {
            self.frames.len()
        } else {
            // Don't need to produce a frame
            return Ok(vec![]);
        };
        let mut header = RtpHeader([0; RTP_HEADER_LEN]);
        header.set_version(RTP_VERSION);
        header.set_payload_type(RTP_PAYLOAD_DYNAMIC);
        header.set_sequence_number(self.next_sequence_number);
        header.set_timestamp(self.timestamp);
        let header_iter = header.0.iter().cloned();
        let frame_header_iter = self.frame_header.iter().cloned();
        let frame_bytes_iter = self.frames.drain(..packet_ready_idx).flatten();
        let packet = header_iter.chain(frame_header_iter).chain(frame_bytes_iter).collect();
        self.next_sequence_number = self.next_sequence_number.wrapping_add(1);
        let frame_samples: u32 = self.frame_samples.drain(..packet_ready_idx).sum();
        self.timestamp = self.timestamp + frame_samples;
        Ok(vec![packet])
    }
}

/// A packetbuilder that works for MPEG-2,4 AAC as specified in A2DP 1.3.1 Section 4.5 and
/// RFC 3016.
pub(crate) struct AacRtpPacketBuilder {
    /// The maximum size of a RTP packet, including headers.
    max_packet_size: usize,
    /// The next sequence number to be used
    next_sequence: u16,
    /// Timestamp of the last packet sent so far.
    timestamp: u32,
}

impl AacRtpPacketBuilder {
    pub fn new(max_packet_size: usize) -> Self {
        Self { max_packet_size, next_sequence: 1, timestamp: 0 }
    }
}

impl RtpPacketBuilder for AacRtpPacketBuilder {
    fn add_frame(&mut self, mut frame: Vec<u8>, samples: u32) -> Result<Vec<Vec<u8>>, Error> {
        let mut header = RtpHeader([0; RTP_HEADER_LEN]);
        header.set_version(RTP_VERSION);
        header.set_payload_type(RTP_PAYLOAD_DYNAMIC);
        header.set_timestamp(self.timestamp);
        let mut left = frame.len();
        let mut packets = Vec::new();
        while left > 0 {
            let mux_element_space = self.max_packet_size - RTP_HEADER_LEN;
            header.set_sequence_number(self.next_sequence);
            // RFC 3016: If the end of this frame will be included, set the marker to 1
            if left <= mux_element_space {
                header.set_marker(true);
            }
            let header_iter = header.0.iter().cloned();
            let packet_frame_end = min(mux_element_space, frame.len());
            let frame_bytes_iter = frame.drain(..packet_frame_end);
            let packet = header_iter.chain(frame_bytes_iter).collect();
            packets.push(packet);
            left = left.saturating_sub(packet_frame_end);
            self.next_sequence = self.next_sequence.wrapping_add(1);
        }
        self.timestamp = self.timestamp.wrapping_add(samples);
        Ok(packets)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_packet_builder_sbc() {
        let mut builder = FrameRtpPacketBuilder::new(5, 673, vec![5]);

        assert!(builder.add_frame(vec![0xf0], 1).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x9f], 2).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x92], 4).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x96], 8).unwrap().is_empty());

        let mut result = builder.add_frame(vec![0x33], 16).expect("no error");

        let expected = &[
            0x80, 0x60, 0x00, 0x01, // Sequence num
            0x00, 0x00, 0x00, 0x00, // timestamp = 0
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x05, // Frames in this packet
            0xf0, 0x9f, 0x92, 0x96, 0x33, // 💖!
        ];

        let result = result.drain(..).next().expect("a packet after 5 more frames");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);

        assert!(builder.add_frame(vec![0xf0], 32).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x9f], 64).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x92], 128).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x96], 256).unwrap().is_empty());

        let mut result = builder.add_frame(vec![0x33], 512).expect("no error");

        let expected = &[
            0x80, 0x60, 0x00, 0x02, // Sequence num
            0x00, 0x00, 0x00, 0x1F, // timestamp = 2^0 + 2^1 + 2^2 + 2^3 + 2^4)
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x05, // Frames in this packet
            0xf0, 0x9f, 0x92, 0x96, 0x33, // 💖!
        ];

        let result = result.drain(..).next().expect("a packet after 5 more frames");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);
    }

    #[test]
    fn test_packet_builder_max_len() {
        // Max size 16 = header (12) + 4
        let mut builder = FrameRtpPacketBuilder::new(5, 4 + RTP_HEADER_LEN, vec![]);

        assert!(builder.add_frame(vec![0xf0], 1).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x9f], 2).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x92], 4).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x96], 8).unwrap().is_empty());

        let mut result = builder.add_frame(vec![0xf0], 16).expect("no error");

        let expected = &[
            0x80, 0x60, 0x00, 0x01, // Sequence num
            0x00, 0x00, 0x00, 0x00, // timestamp = 0
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0xf0, 0x9f, 0x92, 0x96, // 💖
        ];

        let result = result.drain(..).next().expect("a packet after max size is reached");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);

        assert!(builder.add_frame(vec![0x9f, 0x92, 0x96], 32).unwrap().is_empty());

        let mut result = builder.add_frame(vec![0x33], 64).expect("no error");

        let expected = &[
            0x80, 0x60, 0x00, 0x02, // Sequence num
            0x00, 0x00, 0x00, 0x0F, // timestamp = 2^0 + 2^1 + 2^2 + 2^3 (sent last time)
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0xf0, 0x9f, 0x92, 0x96, // 💖
        ];

        let result = result.drain(..).next().expect("a packet after 4 more bytes");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);

        // Trying to push a packet that will never fit in the max returns an error.
        let result = builder.add_frame(vec![0x01, 0x02, 0x03, 0x04, 0x05], 512);
        assert!(result.is_err());
    }

    #[test]
    fn aac_packet_buikder() {
        let mut builder = AacRtpPacketBuilder::new(5 + RTP_HEADER_LEN);

        // One packet has at max one frame in it, so it is returned immediately.
        let result = builder
            .add_frame(vec![0xf0], 1)
            .unwrap()
            .drain(..)
            .next()
            .expect("should return a frame");

        let expected = &[
            0x80, // Version=2, padding, extension (0), csrc count (0),
            0xE0, // Marker (1),  Payload Type (96)
            0x00, 0x01, // Sequence num
            0x00, 0x00, 0x00, 0x00, // timestamp = 0
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0xf0, // frame payload.
        ];

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);

        let result = builder
            .add_frame(vec![0xf0, 0x9f, 0x92, 0x96], 2)
            .unwrap()
            .drain(..)
            .next()
            .expect("should return a frame");

        let expected = &[
            0x80, // Version=2, padding, extension (0), csrc count (0),
            0xE0, // Marker (1),  Payload Type (96)
            0x00, 0x02, // Sequence num
            0x00, 0x00, 0x00, 0x01, // timestamp = 1
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0xf0, 0x9f, 0x92, 0x96, // frame payload.
        ];

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);
    }

    #[test]
    fn aac_packet_builder_fragmentation() {
        let mut builder = AacRtpPacketBuilder::new(2 + RTP_HEADER_LEN);

        // One packet has at max one frame in it, so it is returned immediately.
        let frames = builder.add_frame(vec![0xf0, 0x9f, 0x92, 0x96], 1).unwrap();

        assert_eq!(2, frames.len());

        let first_expected = &[
            0x80, // Version=2, padding, extension (0), csrc count (0),
            0x60, // Marker (0),  Payload Type (96)
            0x00, 0x01, // Sequence num
            0x00, 0x00, 0x00, 0x00, // timestamp = 0
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0xf0, 0x9f, // frame payload.
        ];

        assert_eq!(first_expected.len(), frames[0].len());
        assert_eq!(first_expected, &frames[0][0..first_expected.len()]);

        let second_expected = &[
            0x80, // Version=2, padding, extension (0), csrc count (0),
            0xE0, // Marker (1),  Payload Type (96)
            0x00, 0x02, // Sequence num
            0x00, 0x00, 0x00, 0x00, // timestamp = 0
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x92, 0x96, // frame payload.
        ];

        assert_eq!(second_expected.len(), frames[1].len());
        assert_eq!(second_expected, &frames[1][0..second_expected.len()]);

        let frames = builder.add_frame(vec![0xf0, 0x9f], 2).unwrap();

        assert_eq!(1, frames.len());

        let result = frames.first().unwrap();

        let expected = &[
            0x80, // Version=2, padding, extension (0), csrc count (0),
            0xE0, // Marker (1),  Payload Type (96)
            0x00, 0x03, // Sequence num
            0x00, 0x00, 0x00, 0x01, // timestamp = 1
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0xf0, 0x9f, // frame payload.
        ];

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);
    }
}
