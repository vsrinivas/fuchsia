// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{labels::NodeId, ping_tracker::Pong};
use anyhow::{format_err, Error};
use byteorder::{ReadBytesExt, WriteBytesExt};
use rand::Rng;

/// Labels where a packet is going to
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RoutingDestination {
    /// Messages can be routed over links - argument is the destination node.
    Message(NodeId),
    /// Link control messages are always peer-to-peer.
    Control,
}

/// Labels where a packet is coming from and going to
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RoutingTarget {
    pub src: NodeId,
    pub dst: RoutingDestination,
}

/// Labels a link frame
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LinkFrameLabel {
    /// Where should this frame be delivered?
    pub target: RoutingTarget,
    /// Ping id (if a ping is requested)
    pub ping: Option<u64>,
    /// Pong id (if a pong is being sent)
    pub pong: Option<Pong>,
    /// Debug string
    pub debug_token: Option<u64>,
}

const LINK_FRAME_LABEL_HAS_SRC: u8 = 0x01;
const LINK_FRAME_LABEL_HAS_DST: u8 = 0x02;
const LINK_FRAME_LABEL_IS_CONTROL: u8 = 0x04;
const LINK_FRAME_LABEL_HAS_PING: u8 = 0x10;
const LINK_FRAME_LABEL_HAS_PONG: u8 = 0x20;
const LINK_FRAME_LABEL_HAS_DEBUG_TOKEN: u8 = 0x80;

// One control byte, then up to 6 u64's @ 8 bytes each
pub const LINK_FRAME_LABEL_MAX_SIZE: usize = 1 + 6 * 8;

impl LinkFrameLabel {
    pub fn new_debug_token() -> Option<u64> {
        Some(rand::thread_rng().gen::<u64>())
    }

    /// Encode this into `buf`.
    /// `link_src` and `link_dst` represent the endpoints of the link that will send this label.
    /// Since there are compression options available for the endpoints in the wire format, these
    /// allow the encoder to optimize the encoding for size.
    pub fn encode_for_link(
        &self,
        link_src: NodeId,
        force_write_src: bool,
        link_dst: Option<NodeId>,
        mut buf: impl std::io::Write,
    ) -> Result<usize, Error> {
        log::trace!("ENCODE {:?}", self);
        let mut control: u8 = 0;
        let mut length: usize = 0;
        let src = self.target.src;
        match self.target.dst {
            RoutingDestination::Message(dst) => {
                let link_dst = link_dst.unwrap();
                if src == dst {
                    anyhow::bail!(
                        "Routing labels cannot have the same source and destination address"
                    );
                }
                assert!(src != link_dst);
                if link_dst != dst {
                    control |= LINK_FRAME_LABEL_HAS_DST;
                    length += 8;
                    buf.write_u64::<byteorder::LittleEndian>(dst.0)?;
                }
            }
            RoutingDestination::Control => {
                control |= LINK_FRAME_LABEL_IS_CONTROL;
            }
        }
        if force_write_src || link_src != src {
            control |= LINK_FRAME_LABEL_HAS_SRC;
            length += 8;
            buf.write_u64::<byteorder::LittleEndian>(src.0)?;
        }
        if let Some(id) = self.ping {
            control |= LINK_FRAME_LABEL_HAS_PING;
            length += 8;
            buf.write_u64::<byteorder::LittleEndian>(id)?;
        }
        if let Some(pong) = self.pong {
            control |= LINK_FRAME_LABEL_HAS_PONG;
            length += 16;
            buf.write_u64::<byteorder::LittleEndian>(pong.id)?;
            buf.write_u64::<byteorder::LittleEndian>(pong.queue_time)?;
        }
        if let Some(id) = self.debug_token {
            control |= LINK_FRAME_LABEL_HAS_DEBUG_TOKEN;
            length += 8;
            buf.write_u64::<byteorder::LittleEndian>(id)?;
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
        link_src: Option<NodeId>,
        link_dst: NodeId,
        buf: &[u8],
    ) -> Result<(LinkFrameLabel, usize), Error> {
        let mut r = ReverseReader(buf);
        let control = r.rd_u8()?;
        log::trace!("control={:x} link_src={:?} link_dst={:?}", control, link_src, link_dst);
        if control & LINK_FRAME_LABEL_IS_CONTROL != 0 {
            if control & LINK_FRAME_LABEL_HAS_DST != 0 {
                anyhow::bail!("Control messages cannot specify dst");
            }
        }
        let h = LinkFrameLabel {
            debug_token: if (control & LINK_FRAME_LABEL_HAS_DEBUG_TOKEN) != 0 {
                Some(r.rd_u64()?.into())
            } else {
                None
            },
            pong: if (control & LINK_FRAME_LABEL_HAS_PONG) != 0 {
                Some(Pong { queue_time: r.rd_u64()?.into(), id: r.rd_u64()?.into() })
            } else {
                None
            },
            ping: if (control & LINK_FRAME_LABEL_HAS_PING) != 0 {
                Some(r.rd_u64()?.into())
            } else {
                None
            },
            target: {
                let src = if (control & LINK_FRAME_LABEL_HAS_SRC) != 0 {
                    r.rd_u64()?.into()
                } else {
                    link_src.ok_or(format_err!(
                        "Received routable message with compressed source address and no known link source"
                    ))?
                };
                let dst = if (control & LINK_FRAME_LABEL_IS_CONTROL) != 0 {
                    RoutingDestination::Control
                } else {
                    let dst = if (control & LINK_FRAME_LABEL_HAS_DST) != 0 {
                        r.rd_u64()?.into()
                    } else {
                        link_dst
                    };
                    if src == dst {
                        anyhow::bail!(
                            "Routing labels cannot have the same source and destination address"
                        );
                    }
                    RoutingDestination::Message(dst)
                };
                RoutingTarget { src, dst }
            },
        };
        Ok((h, r.0.len()))
    }
}

// Helper for LinkFrameLabel.decode
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

    fn round_trips_buf(r: LinkFrameLabel, link_src: NodeId, link_dst: NodeId) {
        log::trace!("Check roundtrips: {:?} with src={:?} dst={:?}", r, link_src, link_dst);
        let mut buf: [u8; 128] = [0; 128];
        let suffix_len =
            r.encode_for_link(link_src, false, Some(link_dst), &mut buf[10..]).unwrap();
        log::trace!("Encodes to: {:?}", &buf[10..10 + suffix_len]);
        assert_eq!(buf[0..10].to_vec(), vec![0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
        let (q, len) =
            LinkFrameLabel::decode(Some(link_src), link_dst, &buf[0..10 + suffix_len]).unwrap();
        assert_eq!(len, 10);
        assert_eq!(r, q);
    }

    fn round_trips(r: LinkFrameLabel, link_src: NodeId, link_dst: NodeId) {
        round_trips_buf(r, link_src, link_dst);
    }

    #[fuchsia::test]
    fn round_trip_examples() {
        round_trips(
            LinkFrameLabel {
                target: RoutingTarget { src: 1.into(), dst: RoutingDestination::Message(2.into()) },
                ping: Some(3),
                pong: None,
                debug_token: None,
            },
            1.into(),
            2.into(),
        );
        round_trips(
            LinkFrameLabel {
                target: RoutingTarget { src: 1.into(), dst: RoutingDestination::Control },
                ping: None,
                pong: None,
                debug_token: None,
            },
            1.into(),
            2.into(),
        );
        round_trips(
            LinkFrameLabel {
                target: RoutingTarget { src: 1.into(), dst: RoutingDestination::Message(3.into()) },
                ping: None,
                pong: None,
                debug_token: None,
            },
            1.into(),
            2.into(),
        );
        round_trips(
            LinkFrameLabel {
                target: RoutingTarget { src: 1.into(), dst: RoutingDestination::Message(3.into()) },
                ping: None,
                pong: None,
                debug_token: LinkFrameLabel::new_debug_token(),
            },
            1.into(),
            2.into(),
        );
        round_trips(
            LinkFrameLabel {
                target: RoutingTarget { src: 3.into(), dst: RoutingDestination::Message(2.into()) },
                ping: Some(1),
                pong: None,
                debug_token: LinkFrameLabel::new_debug_token(),
            },
            1.into(),
            2.into(),
        );
        round_trips(
            LinkFrameLabel {
                target: RoutingTarget { src: 3.into(), dst: RoutingDestination::Message(2.into()) },
                ping: None,
                pong: Some(Pong { id: 1, queue_time: 999 }),
                debug_token: LinkFrameLabel::new_debug_token(),
            },
            1.into(),
            2.into(),
        );
        round_trips(
            LinkFrameLabel {
                target: RoutingTarget { src: 1.into(), dst: RoutingDestination::Message(2.into()) },
                ping: Some(123),
                pong: Some(Pong { id: 456, queue_time: 789 }),
                debug_token: LinkFrameLabel::new_debug_token(),
            },
            3.into(),
            4.into(),
        );
        round_trips(
            LinkFrameLabel {
                target: RoutingTarget { src: 1.into(), dst: RoutingDestination::Message(2.into()) },
                ping: None,
                pong: None,
                debug_token: LinkFrameLabel::new_debug_token(),
            },
            3.into(),
            4.into(),
        );
    }
}
