// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ping_tracker::Pong;
use anyhow::Error;
use byteorder::{ReadBytesExt, WriteBytesExt};
use rand::Rng;

/// Labels a node with a mesh-unique address
#[derive(PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug, Hash)]
pub struct NodeId(pub u64);

impl From<u64> for NodeId {
    fn from(id: u64) -> Self {
        NodeId(id)
    }
}

impl From<NodeId> for fidl_fuchsia_overnet_protocol::NodeId {
    fn from(id: NodeId) -> Self {
        Self { id: id.0 }
    }
}

/// Labels a link with a node-unique identifier
#[derive(PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug)]
pub struct NodeLinkId(pub u64);

impl From<u64> for NodeLinkId {
    fn from(id: u64) -> Self {
        NodeLinkId(id)
    }
}

/// Labels where a packet is coming from/going to
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RoutingLabel {
    /// Source node
    pub src: NodeId,
    /// Destination node
    pub dst: NodeId,
    /// Ping id (if a ping is requested)
    pub ping: Option<u64>,
    /// Pong id (if a pong is being sent)
    pub pong: Option<Pong>,
    /// Debug string
    pub debug_token: Option<u64>,
    /// Is this being routed to a client connection or a client connection?
    /// (see discussion at head of router.rs)
    pub to_client: bool,
}

pub const ROUTING_LABEL_HAS_SRC: u8 = 0x01;
pub const ROUTING_LABEL_HAS_DST: u8 = 0x02;
pub const ROUTING_LABEL_TO_CLIENT: u8 = 0x04;
pub const ROUTING_LABEL_HAS_PING: u8 = 0x10;
pub const ROUTING_LABEL_HAS_PONG: u8 = 0x20;
pub const ROUTING_LABEL_HAS_DEBUG_TOKEN: u8 = 0x80;

pub const MAX_ROUTING_LABEL_LENGTH: usize = 8 + 8 + 8 + 16 + 8 + 1;

pub fn new_debug_token() -> Option<u64> {
    Some(rand::thread_rng().gen::<u64>())
}

impl RoutingLabel {
    /// Encode this into `buf`.
    /// `link_src` and `link_dst` represent the endpoints of the link that will send this label.
    /// Since there are compression options available for the endpoints in the wire format, these
    /// allow the encoder to optimize the encoding for size.
    pub fn encode_for_link(
        &self,
        link_src: NodeId,
        link_dst: NodeId,
        mut buf: impl std::io::Write,
    ) -> Result<usize, Error> {
        log::trace!("ENCODE {:?}", self);
        let mut control: u8 = 0;
        let mut length: usize = 0;
        if link_src != self.src {
            control |= ROUTING_LABEL_HAS_SRC;
            length += 8;
            buf.write_u64::<byteorder::LittleEndian>(self.src.0)?;
        }
        if link_dst != self.dst {
            control |= ROUTING_LABEL_HAS_DST;
            length += 8;
            buf.write_u64::<byteorder::LittleEndian>(self.dst.0)?;
        }
        if let Some(id) = self.ping {
            control |= ROUTING_LABEL_HAS_PING;
            length += 8;
            buf.write_u64::<byteorder::LittleEndian>(id)?;
        }
        if let Some(pong) = self.pong {
            control |= ROUTING_LABEL_HAS_PONG;
            length += 16;
            buf.write_u64::<byteorder::LittleEndian>(pong.id)?;
            buf.write_u64::<byteorder::LittleEndian>(pong.queue_time)?;
        }
        if let Some(id) = self.debug_token {
            control |= ROUTING_LABEL_HAS_DEBUG_TOKEN;
            length += 8;
            buf.write_u64::<byteorder::LittleEndian>(id)?;
        }
        if self.to_client {
            control |= ROUTING_LABEL_TO_CLIENT;
        }
        log::trace!("control={:x} link_src={:?} link_dst={:?}", control, link_src, link_dst);
        length += 1;
        buf.write_u8(control)?;
        Ok(length)
    }

    /// Decode a routing label from `buf`.
    /// `link_src` and `link_dst` represent the endpoints of the link that will send this label.
    /// Since there are compression options available for the endpoints in the wire format, these
    /// are necessary to decode the encoded form.
    pub fn decode(
        link_src: NodeId,
        link_dst: NodeId,
        buf: &[u8],
    ) -> Result<(RoutingLabel, usize), Error> {
        let mut r = ReverseReader(buf);
        let control = r.rd_u8()?;
        log::trace!("control={:x} link_src={:?} link_dst={:?}", control, link_src, link_dst);
        let h = RoutingLabel {
            to_client: (control & ROUTING_LABEL_TO_CLIENT) != 0,
            debug_token: if (control & ROUTING_LABEL_HAS_DEBUG_TOKEN) != 0 {
                Some(r.rd_u64()?.into())
            } else {
                None
            },
            pong: if (control & ROUTING_LABEL_HAS_PONG) != 0 {
                Some(Pong { queue_time: r.rd_u64()?.into(), id: r.rd_u64()?.into() })
            } else {
                None
            },
            ping: if (control & ROUTING_LABEL_HAS_PING) != 0 {
                Some(r.rd_u64()?.into())
            } else {
                None
            },
            dst: if (control & ROUTING_LABEL_HAS_DST) != 0 { r.rd_u64()?.into() } else { link_dst },
            src: if (control & ROUTING_LABEL_HAS_SRC) != 0 { r.rd_u64()?.into() } else { link_src },
        };
        Ok((h, r.0.len()))
    }
}

// Helper for RoutingLabel.decode
struct ReverseReader<'a>(&'a [u8]);

impl<'a> ReverseReader<'a> {
    fn rd_u8(&mut self) -> Result<u8, Error> {
        Ok(self.take(1)?.read_u8()?)
    }

    fn rd_u64(&mut self) -> Result<u64, Error> {
        Ok(self.take(8)?.read_u64::<byteorder::LittleEndian>()?)
    }

    fn take(&mut self, amt: usize) -> Result<&[u8], Error> {
        let len = self.0.len();
        if len < amt {
            return Err(anyhow::format_err!(
                "Tried to read {} bytes from {} byte buffer",
                amt,
                len
            ));
        }
        let (head, tail) = self.0.split_at(len - amt);
        self.0 = head;
        Ok(tail)
    }
}

#[cfg(test)]
mod test {

    use super::*;

    fn round_trips_buf(r: RoutingLabel, link_src: NodeId, link_dst: NodeId) {
        log::trace!("Check roundtrips: {:?} with src={:?} dst={:?}", r, link_src, link_dst);
        let mut buf: [u8; 128] = [0; 128];
        let suffix_len = r.encode_for_link(link_src, link_dst, &mut buf[10..]).unwrap();
        log::trace!("Encodes to: {:?}", &buf[10..10 + suffix_len]);
        assert_eq!(buf[0..10].to_vec(), vec![0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
        let (q, len) = RoutingLabel::decode(link_src, link_dst, &buf[0..10 + suffix_len]).unwrap();
        assert_eq!(len, 10);
        assert_eq!(r, q);
    }

    fn round_trips(r: RoutingLabel, link_src: NodeId, link_dst: NodeId) {
        round_trips_buf(r, link_src, link_dst);
    }

    #[test]
    fn round_trip_examples() {
        round_trips(
            RoutingLabel {
                to_client: false,
                src: 1.into(),
                dst: 2.into(),
                ping: Some(3),
                pong: None,
                debug_token: None,
            },
            1.into(),
            2.into(),
        );
        round_trips(
            RoutingLabel {
                to_client: false,
                src: 1.into(),
                dst: 2.into(),
                ping: None,
                pong: None,
                debug_token: None,
            },
            1.into(),
            2.into(),
        );
        round_trips(
            RoutingLabel {
                to_client: true,
                src: 1.into(),
                dst: 2.into(),
                ping: None,
                pong: None,
                debug_token: None,
            },
            1.into(),
            2.into(),
        );
        round_trips(
            RoutingLabel {
                to_client: false,
                src: 1.into(),
                dst: 3.into(),
                ping: None,
                pong: None,
                debug_token: None,
            },
            1.into(),
            2.into(),
        );
        round_trips(
            RoutingLabel {
                to_client: true,
                src: 1.into(),
                dst: 3.into(),
                ping: None,
                pong: None,
                debug_token: new_debug_token(),
            },
            1.into(),
            2.into(),
        );
        round_trips(
            RoutingLabel {
                to_client: false,
                src: 3.into(),
                dst: 2.into(),
                ping: Some(1),
                pong: None,
                debug_token: new_debug_token(),
            },
            1.into(),
            2.into(),
        );
        round_trips(
            RoutingLabel {
                to_client: true,
                src: 3.into(),
                dst: 2.into(),
                ping: None,
                pong: Some(Pong { id: 1, queue_time: 999 }),
                debug_token: new_debug_token(),
            },
            1.into(),
            2.into(),
        );
        round_trips(
            RoutingLabel {
                to_client: false,
                src: 1.into(),
                dst: 2.into(),
                ping: Some(123),
                pong: Some(Pong { id: 456, queue_time: 789 }),
                debug_token: new_debug_token(),
            },
            3.into(),
            4.into(),
        );
        round_trips(
            RoutingLabel {
                to_client: true,
                src: 1.into(),
                dst: 2.into(),
                ping: None,
                pong: None,
                debug_token: new_debug_token(),
            },
            3.into(),
            4.into(),
        );
    }
}
