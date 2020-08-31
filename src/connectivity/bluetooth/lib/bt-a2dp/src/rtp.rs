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

/// A PacketBuilder that implements the SBC RTP Payload format as specified in A2DP 1.3.2 Section
/// 4.3.4.
pub(crate) struct SbcRtpPacketBuilder {
    /// maximum size of the RTP packet, including headers
    max_packet_size: usize,
    /// next sequence number to be used
    next_sequence: u16,
    /// Timestamp of the last packet sent so far, in audio sample units
    timestamp: u32,
    /// Frames that will be in the next RtpPacket to be sent.
    frames: VecDeque<Vec<u8>>,
    /// Total samples that those frames represent.
    samples: u32,
}

impl SbcRtpPacketBuilder {
    const MAX_FRAMES_PER_PACKET: usize = 15;

    pub fn new(max_packet_size: usize) -> Self {
        Self {
            max_packet_size,
            next_sequence: 1,
            timestamp: 0,
            frames: VecDeque::new(),
            samples: 0,
        }
    }

    /// Return the header length of a RTP packet encapsulating SBC, including the pre-frame
    /// data, which is one octet long.
    fn header_len(&self) -> usize {
        RTP_HEADER_LEN + 1
    }

    /// The total number of bytes currently waiting to be sent in the next frame.
    fn frame_bytes_len(&self) -> usize {
        self.frames.iter().map(Vec::len).sum()
    }

    /// Build the next RTP packet using the frames queued.
    /// After the packet is returned, the frames are clear and the state is updated to
    /// track building the next packet.
    /// Panics if called when there are no frames to be sent.
    fn build_packet(&mut self) -> Vec<u8> {
        if self.frames.is_empty() {
            panic!("Can't build an empty RTP SBC packet: no frames");
        }
        let mut header = RtpHeader([0; RTP_HEADER_LEN]);
        header.set_version(RTP_VERSION);
        header.set_payload_type(RTP_PAYLOAD_DYNAMIC);
        header.set_sequence_number(self.next_sequence);
        header.set_timestamp(self.timestamp);
        let mut packet: Vec<u8> = header.0.iter().cloned().collect();
        packet.push(self.frames.len() as u8);
        let mut frames_bytes = self.frames.drain(..).flatten().collect();
        packet.append(&mut frames_bytes);

        self.next_sequence = self.next_sequence.wrapping_add(1);
        self.timestamp = self.timestamp.wrapping_add(self.samples);
        self.samples = 0;

        packet
    }
}

impl RtpPacketBuilder for SbcRtpPacketBuilder {
    fn add_frame(&mut self, frame: Vec<u8>, samples: u32) -> Result<Vec<Vec<u8>>, Error> {
        if (frame.len() + self.header_len()) > self.max_packet_size {
            return Err(format_err!("Media packet too large for RTP max size"));
        }
        let mut packets = Vec::new();
        let packet_size_with_new = self.header_len() + self.frame_bytes_len() + frame.len();
        if packet_size_with_new > self.max_packet_size {
            packets.push(self.build_packet());
        }
        self.frames.push_back(frame);
        self.samples = self.samples + samples;
        if self.frames.len() == Self::MAX_FRAMES_PER_PACKET {
            packets.push(self.build_packet());
        }
        Ok(packets)
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
    fn sbc_packet_builder_max_len() {
        // 4 bytes leeway for the frames, plus one byte for the payload header.
        let mut builder = SbcRtpPacketBuilder::new(RTP_HEADER_LEN + 5);

        assert!(builder.add_frame(vec![0xf0], 1).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x9f], 2).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x92], 4).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x96], 8).unwrap().is_empty());

        let mut result = builder.add_frame(vec![0xf0], 16).expect("no error");

        let expected = &[
            0x80, 0x60, 0x00, 0x01, // Sequence num
            0x00, 0x00, 0x00, 0x00, // timestamp = 0
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x04, // Frames in this packet
            0xf0, 0x9f, 0x92, 0x96, // 💖
        ];

        let result = result.drain(..).next().expect("a packet after 4 frames");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);

        assert!(builder.add_frame(vec![0x9f], 32).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x92], 64).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x96], 128).unwrap().is_empty());

        let mut result = builder.add_frame(vec![0x33], 256).expect("no error");

        let expected = &[
            0x80, 0x60, 0x00, 0x02, // Sequence num
            0x00, 0x00, 0x00, 0x0F, // timestamp = 2^0 + 2^1 + 2^2 + 2^3)
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x04, // Frames in this packet
            0xf0, 0x9f, 0x92, 0x96, // 💖
        ];

        let result = result.drain(..).next().expect("a packet after 4 more frames");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);

        // Trying to push a packet that will never fit in the max returns an error.
        let result = builder.add_frame(vec![0x01, 0x02, 0x03, 0x04, 0x05], 512);
        assert!(result.is_err());
    }

    #[test]
    fn sbc_packet_builder_max_frames() {
        let max_tx_size = 200;
        let mut builder = SbcRtpPacketBuilder::new(max_tx_size);

        assert!(builder.add_frame(vec![0xf0], 1).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x9f], 2).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x92], 4).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x96], 8).unwrap().is_empty());
        assert!(builder.add_frame(vec![0xf0], 16).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x9f], 32).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x92], 64).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x96], 128).unwrap().is_empty());
        assert!(builder.add_frame(vec![0xf0], 256).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x9f], 512).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x92], 1024).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x96], 2048).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x33], 4096).unwrap().is_empty());
        assert!(builder.add_frame(vec![0x33], 8192).unwrap().is_empty());

        let mut result = builder.add_frame(vec![0x33], 16384).expect("no error");

        let expected = &[
            0x80, 0x60, 0x00, 0x01, // Sequence num
            0x00, 0x00, 0x00, 0x00, // timestamp = 0
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x0F, // 15 frames in this packet
            0xf0, 0x9f, 0x92, 0x96, // 💖
            0xf0, 0x9f, 0x92, 0x96, // 💖
            0xf0, 0x9f, 0x92, 0x96, // 💖
            0x33, 0x33, 0x33, // !!!
        ];

        let result = result.drain(..).next().expect("should have a packet");

        assert_eq!(expected.len(), result.len());
        assert_eq!(expected, &result[0..expected.len()]);

        assert!(builder.add_frame(vec![0xf0, 0x9f, 0x92, 0x96], 32768).unwrap().is_empty());
        let rest_of_packet: Vec<u8> = (4..(max_tx_size - RTP_HEADER_LEN - 1) as u8).collect();
        assert!(builder.add_frame(rest_of_packet, 65536).unwrap().is_empty());

        let mut result = builder.add_frame(vec![0x33], 131072).expect("no error");

        let expected = &[
            0x80, 0x60, 0x00, 0x02, // Sequence num
            0x00, 0x00, 0x7F, 0xFF, // timestamp = 2^0 + 2^1 + ... + 2^14 (sent last time)
            0x00, 0x00, 0x00, 0x00, // SSRC = 0
            0x02, // 2 frames in this packet
            0xf0, 0x9f, 0x92, 0x96, // 💖
        ];

        let result = result.drain(..).next().expect("a packet after max bytes exceeded");

        assert!(expected.len() <= result.len());
        assert_eq!(expected, &result[0..expected.len()]);
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
