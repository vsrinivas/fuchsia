// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Framing and deframing datagrams onto QUIC streams

use crate::{
    async_quic::{AsyncConnection, AsyncQuicStreamReader, AsyncQuicStreamWriter, StreamProperties},
    stat_counter::StatCounter,
};
use anyhow::{bail, Error};
use std::convert::TryInto;

/// The type of frame that can be received on a QUIC stream
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum FrameType {
    Hello,
    Data,
    Control,
}

/// Header for one frame of data on a QUIC stream
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct FrameHeader {
    /// Type of the frame
    frame_type: FrameType,
    /// Length of the frame (usize here to avoid casts in client code; this is checked to fit in a
    /// u32 before serialization)
    length: usize,
}

const FRAME_HEADER_LENGTH: usize = 8;

impl FrameHeader {
    fn to_bytes(&self) -> Result<[u8; FRAME_HEADER_LENGTH], Error> {
        let length = self.length;
        if length > std::u32::MAX as usize {
            return Err(anyhow::format_err!("Message too long: {}", length));
        }
        let length = length as u32;
        let hdr: u64 = (length as u64)
            | (match self.frame_type {
                FrameType::Hello => 0,
                FrameType::Data => 1,
                FrameType::Control => 2,
            } << 32);
        Ok(hdr.to_le_bytes())
    }

    fn from_bytes(bytes: &[u8]) -> Result<Self, Error> {
        let hdr: &[u8; FRAME_HEADER_LENGTH] = bytes[0..FRAME_HEADER_LENGTH].try_into()?;
        let hdr = u64::from_le_bytes(*hdr);
        let length = (hdr & 0xffff_ffff) as usize;
        let frame_type = match hdr >> 32 {
            0 => FrameType::Hello,
            1 => FrameType::Data,
            2 => FrameType::Control,
            _ => return Err(anyhow::format_err!("Unknown frame type {}", hdr >> 32)),
        };
        Ok(FrameHeader { frame_type, length })
    }
}

#[derive(Default, Debug)]
pub struct MessageStats {
    sent_bytes: StatCounter,
    sent_messages: StatCounter,
}

impl MessageStats {
    fn sent_message(&self, bytes: u64) {
        self.sent_messages.inc();
        self.sent_bytes.add(bytes);
    }

    pub fn sent_bytes(&self) -> u64 {
        self.sent_bytes.fetch()
    }

    pub fn sent_messages(&self) -> u64 {
        self.sent_messages.fetch()
    }
}

#[derive(Debug)]
pub(crate) struct FramedStreamWriter {
    /// The underlying QUIC stream
    quic: AsyncQuicStreamWriter,
}

impl From<AsyncQuicStreamWriter> for FramedStreamWriter {
    fn from(quic: AsyncQuicStreamWriter) -> Self {
        Self { quic }
    }
}

impl FramedStreamWriter {
    pub(crate) fn abandon(&mut self) {
        self.quic.abandon()
    }

    pub(crate) async fn send(
        &mut self,
        frame_type: FrameType,
        bytes: &[u8],
        fin: bool,
        message_stats: &MessageStats,
    ) -> Result<(), Error> {
        let r = self.send_inner(frame_type, bytes, fin, message_stats).await;
        if r.is_err() {
            self.abandon();
        }
        r
    }

    async fn send_inner(
        &mut self,
        frame_type: FrameType,
        bytes: &[u8],
        fin: bool,
        message_stats: &MessageStats,
    ) -> Result<(), Error> {
        let frame_len = bytes.len();
        assert!(frame_len <= 0xffff_ffff);
        let mut header = FrameHeader { frame_type, length: frame_len }.to_bytes()?;
        log::trace!("header: {:?}", header);
        self.quic.send(&mut header, false).await?;
        if bytes.len() > 0 {
            self.quic.send(bytes, fin).await?;
        }
        message_stats.sent_message(header.len() as u64 + bytes.len() as u64);
        Ok(())
    }
}

impl StreamProperties for FramedStreamWriter {
    fn id(&self) -> u64 {
        self.quic.id()
    }

    fn conn(&self) -> &AsyncConnection {
        self.quic.conn()
    }
}

#[derive(Debug)]
pub(crate) struct FramedStreamReader {
    /// The underlying QUIC stream
    quic: AsyncQuicStreamReader,
}

impl From<AsyncQuicStreamReader> for FramedStreamReader {
    fn from(quic: AsyncQuicStreamReader) -> Self {
        Self { quic }
    }
}

impl FramedStreamReader {
    pub(crate) fn abandon(&mut self) {
        self.quic.abandon()
    }

    pub(crate) async fn next(&mut self) -> Result<(FrameType, Vec<u8>, bool), Error> {
        let r = self.next_inner().await;
        if r.is_err() {
            self.abandon();
        }
        r
    }

    async fn next_inner(&mut self) -> Result<(FrameType, Vec<u8>, bool), Error> {
        let mut hdr = [0u8; FRAME_HEADER_LENGTH];
        let fin = self.quic.read_exact(&mut hdr).await?;
        let hdr = FrameHeader::from_bytes(&hdr)?;
        log::trace!("read header: {:?}", hdr);
        if hdr.length == 0 {
            Ok((hdr.frame_type, Vec::new(), fin))
        } else {
            if fin {
                bail!("Unexpected end of stream");
            }
            let mut backing = vec![0; hdr.length];
            let fin = self.quic.read_exact(&mut backing).await?;
            Ok((hdr.frame_type, backing, fin))
        }
    }
}

impl StreamProperties for FramedStreamReader {
    fn id(&self) -> u64 {
        self.quic.id()
    }

    fn conn(&self) -> &AsyncConnection {
        self.quic.conn()
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn roundtrip(h: FrameHeader) {
        assert_eq!(h, FrameHeader::from_bytes(&h.to_bytes().unwrap()).unwrap());
    }

    #[test]
    fn roundtrips() {
        roundtrip(FrameHeader { frame_type: FrameType::Data, length: 0 });
        roundtrip(FrameHeader { frame_type: FrameType::Data, length: std::u32::MAX as usize });
    }

    #[test]
    fn too_long() {
        FrameHeader { frame_type: FrameType::Data, length: (std::u32::MAX as usize) + 1 }
            .to_bytes()
            .expect_err("Should fail");
    }

    #[test]
    fn bad_frame_type() {
        assert!(format!(
            "{}",
            FrameHeader::from_bytes(&[0, 0, 0, 0, 11, 0, 0, 0]).expect_err("should fail")
        )
        .contains("Unknown frame type 11"));
    }
}
