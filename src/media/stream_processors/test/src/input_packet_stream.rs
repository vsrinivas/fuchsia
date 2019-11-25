use crate::{buffer_set::*, elementary_stream::*};
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;
use fidl_fuchsia_media::*;
use fuchsia_stream_processors::*;
use fuchsia_zircon as zx;
use std::{collections::HashMap, fmt};

type PacketIdx = u32;
type BufferIdx = u32;

/// A stream converting elementary stream chunks into input packets for a stream processor.
pub struct InputPacketStream<I> {
    packet_and_buffer_pairs: HashMap<PacketIdx, (BufferIdx, UsageStatus)>,
    buffer_set: BufferSet,
    stream_lifetime_ordinal: u64,
    stream: I,
    sent_eos: bool,
}

#[derive(Copy, Clone, PartialEq, Debug)]
enum UsageStatus {
    Free,
    InUse,
}

#[derive(Debug)]
pub enum Error {
    PacketRefersToInvalidBuffer,
    BufferTooSmall { buffer_size: usize, stream_chunk_size: usize },
    VmoWriteFail(zx::Status),
}

impl fmt::Display for Error {
    fn fmt(&self, w: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&self, w)
    }
}

impl Fail for Error {}

pub enum PacketPoll {
    Ready(Packet),
    Eos,
    NotReady,
}

impl<'a, I: Iterator<Item = ElementaryStreamChunk<'a>>> InputPacketStream<I> {
    pub fn new(buffer_set: BufferSet, stream: I, stream_lifetime_ordinal: u64) -> Self {
        let packets = 0..(buffer_set.buffers.len() as u32);
        let buffers = packets.clone().rev().map(|idx| (idx, UsageStatus::Free));
        Self {
            packet_and_buffer_pairs: packets.zip(buffers).collect(),
            buffer_set,
            stream_lifetime_ordinal,
            stream,
            sent_eos: false,
        }
    }

    pub fn add_free_packet(&mut self, packet: ValidPacketHeader) -> Result<(), Error> {
        let (_, ref mut status) = *self
            .packet_and_buffer_pairs
            .get_mut(&packet.packet_index)
            .ok_or(Error::PacketRefersToInvalidBuffer)?;
        *status = UsageStatus::Free;
        Ok(())
    }

    fn free_packet_and_buffer(&mut self) -> Option<(u32, u32)> {
        // This is a linear search. This may not be appropriate in prod code.
        self.packet_and_buffer_pairs.iter_mut().find_map(|(packet, (buffer, usage))| match usage {
            UsageStatus::Free => {
                *usage = UsageStatus::InUse;
                Some((*packet, *buffer))
            }
            UsageStatus::InUse => None,
        })
    }

    pub fn next_packet(&mut self) -> Result<PacketPoll, Error> {
        let (packet_idx, buffer_idx) = if let Some(idxs) = self.free_packet_and_buffer() {
            idxs
        } else {
            return Ok(PacketPoll::NotReady);
        };

        let chunk = if let Some(chunk) = self.stream.next() {
            chunk
        } else if !self.sent_eos {
            self.sent_eos = true;
            return Ok(PacketPoll::Eos);
        } else {
            return Ok(PacketPoll::NotReady);
        };

        let buffer = self
            .buffer_set
            .buffers
            .get(buffer_idx as usize)
            .ok_or(Error::PacketRefersToInvalidBuffer)?;

        if (buffer.size as usize) < chunk.data.len() {
            return Err(Error::BufferTooSmall {
                buffer_size: buffer.size as usize,
                stream_chunk_size: chunk.data.len(),
            });
        }

        buffer.data.write(chunk.data, 0).map_err(Error::VmoWriteFail)?;

        Ok(PacketPoll::Ready(Packet {
            header: Some(PacketHeader {
                packet_index: Some(packet_idx),
                buffer_lifetime_ordinal: Some(self.buffer_set.buffer_lifetime_ordinal),
            }),
            buffer_index: Some(buffer_idx),
            stream_lifetime_ordinal: Some(self.stream_lifetime_ordinal),
            start_offset: Some(0),
            valid_length_bytes: Some(chunk.data.len() as u32),
            timestamp_ish: chunk.timestamp,
            start_access_unit: Some(chunk.start_access_unit),
            known_end_access_unit: Some(chunk.known_end_access_unit),
        }))
    }

    pub fn take_buffer_set(self) -> BufferSet {
        self.buffer_set
    }
}
