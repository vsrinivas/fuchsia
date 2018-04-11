// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::BufMut;
use node_id::NodeId;
use sequence_num;
use sequence_num::{SequenceNum, SequenceSerCtx};
use stream_id::StreamId;
use varint;

////////////////////////////////////////////////////////////////////////////////
// externally supplied traits

pub trait HeaderSerCtx {
    type SeqSerCtx: SequenceSerCtx;

    fn get_stream(&self, src: &NodeId, dst: &NodeId, stream: &StreamId) -> &Self::SeqSerCtx;

    fn sender(&self) -> NodeId;
    fn receiver(&self) -> NodeId;
}

////////////////////////////////////////////////////////////////////////////////
// data definitions

// a message is composed of three pieces:
//  1 a routing header exposing which stream this message belongs to, and where
//    in that stream -- no expectation of privacy is given to this piece
//  - a private section that includes:
//    2 private headers - headers that do not need to be read by
//      intermediatories
//    3 application payload (whatever data this message is actually
//      transporting)

pub struct RoutingHeader {
    pub src: NodeId,
    pub dst: NodeId,
    pub stream: StreamId,
    pub seq: SequenceNum,
    pub private_len: u64,
}

pub struct PrivateHeader {
    pub payload_length: Option<u64>,
    pub grant_bytes: u64,
}

////////////////////////////////////////////////////////////////////////////////
// implementation and helper types

pub struct RoutingHeaderWriter<'h> {
    routing_hdr: &'h RoutingHeader,
    is_local: bool,
    stream_writer: varint::Writer,
    seq_writer: sequence_num::Writer<'h>,
    private_length_writer: varint::Writer,
}

pub struct PrivateHeaderWriter {
    private_header_length_writer: varint::Writer,
    grant_bytes_writer: Option<varint::Writer>,
    payload_len_writer: Option<varint::Writer>,
}

fn priv_opt_len(f: &Option<varint::Writer>) -> usize {
    match f {
        &Some(ref l) => 1 + l.len(),
        &None => 0,
    }
}

fn write_opt<B>(buf: &mut B, f: &Option<varint::Writer>, index: u8)
where
    B: BufMut,
{
    match f {
        &Some(ref l) => {
            buf.put_u8(index);
            l.write(buf);
        }
        &None => (),
    }
}

fn sz_opt(f: &Option<varint::Writer>) -> u64 {
    match f {
        &Some(ref l) => 1 + l.len() as u64,
        &None => 0,
    }
}

impl RoutingHeader {
    pub fn writer<'c, 'h, C>(&'h self, ctx: &'c C) -> RoutingHeaderWriter<'h>
    where
        C: 'c,
        C: HeaderSerCtx,
    {
        let is_local = self.src == ctx.sender() && self.dst == ctx.receiver();
        let private_length_writer = varint::Writer::new(self.private_len);
        RoutingHeaderWriter {
            routing_hdr: self,
            is_local,
            seq_writer: self.seq
                .writer(ctx.get_stream(&self.src, &self.dst, &self.stream)),
            stream_writer: self.stream.writer(),
            private_length_writer,
        }
    }
}

impl PrivateHeader {
    pub fn new_with_payload_length(payload_length: u64) -> PrivateHeader {
        PrivateHeader {
            grant_bytes: 0,
            payload_length: Some(payload_length),
        }
    }

    pub fn writer(&self) -> PrivateHeaderWriter {
        let payload_len_writer = match self.payload_length {
            None => None,
            Some(l) => Some(varint::Writer::new(l)),
        };
        let grant_bytes_writer = match self.grant_bytes {
            0 => None,
            l => Some(varint::Writer::new(l)),
        };
        let private_header_length_writer =
            varint::Writer::new((priv_opt_len(&grant_bytes_writer)) as u64);
        PrivateHeaderWriter {
            private_header_length_writer,
            grant_bytes_writer,
            payload_len_writer,
        }
    }
}

impl<'h> RoutingHeaderWriter<'h> {
    pub fn wire_size(&self) -> usize {
        (1 + if !self.is_local {
            self.routing_hdr.src.wire_size() + self.routing_hdr.dst.wire_size()
        } else {
            0
        } + self.stream_writer.len() + self.seq_writer.len()
            + self.private_length_writer.len())
    }

    pub fn write<B>(&self, buf: &mut B)
    where
        B: BufMut,
    {
        debug_assert!(buf.remaining_mut() >= self.wire_size());
        buf.put_u8(bit_if(1, self.is_local));
        if !self.is_local {
            self.routing_hdr.src.write(buf);
            self.routing_hdr.dst.write(buf);
        }
        self.stream_writer.write(buf);
        self.seq_writer.write(buf);
        self.private_length_writer.write(buf);
    }
}

impl PrivateHeaderWriter {
    pub fn wire_size(&self) -> u64 {
        self.private_header_length_writer.len() as u64 + sz_opt(&self.grant_bytes_writer)
            + match &self.payload_len_writer {
                &None => 0,
                &Some(ref l) => l.len() as u64,
            }
    }

    pub fn write<B>(&self, buf: &mut B)
    where
        B: BufMut,
    {
        debug_assert!(buf.remaining_mut() as u64 >= self.wire_size());
        self.private_header_length_writer.write(buf);
        write_opt(buf, &self.grant_bytes_writer, 1);
        match &self.payload_len_writer {
            &None => (),
            &Some(ref b) => b.write(buf),
        };
    }
}

////////////////////////////////////////////////////////////////////////////////
// utilities

fn bit_if<T>(x: T, discrim: bool) -> T
where
    T: From<u8>,
{
    if discrim {
        x
    } else {
        T::from(0)
    }
}

////////////////////////////////////////////////////////////////////////////////
// TESTS

#[cfg(test)]
mod tests {

    use bytes::BufMut;
    use message::{HeaderSerCtx, PrivateHeader, RoutingHeader};
    use node_id::NodeId;
    use sequence_num::{SequenceNum, SequenceSerCtx};
    use std::{io::Cursor, mem};
    use stream_id::{StreamId, StreamType};

    struct Ctx {}
    struct SeqCtx {}

    impl HeaderSerCtx for Ctx {
        type SeqSerCtx = SeqCtx;

        fn get_stream(&self, src: &NodeId, dst: &NodeId, stream: &StreamId) -> &SeqCtx {
            &SEQCTX
        }

        fn sender(&self) -> NodeId {
            1.into()
        }

        fn receiver(&self) -> NodeId {
            2.into()
        }
    }

    impl SequenceSerCtx for SeqCtx {
        fn window(&self) -> (SequenceNum, SequenceNum) {
            (1.into(), 1024.into())
        }
    }

    static CTX: Ctx = Ctx {};
    static SEQCTX: SeqCtx = SeqCtx {};

    fn test_write<'b>(
        buf: &'b mut [u8], src: NodeId, dst: NodeId, stream: StreamId, seq: SequenceNum,
        ph: PrivateHeader, pay: &[u8],
    ) -> &'b [u8] {
        let phw = ph.writer();
        let rh = RoutingHeader {
            src,
            dst,
            stream,
            seq,
            private_len: phw.wire_size() + pay.len() as u64,
        };
        let rhw = rh.writer(&CTX);
        let wire_size = rhw.wire_size() + 2 /* fake privacy markers */ +
                        phw.wire_size() as usize + pay.len();
        let mut outbuf = Cursor::new(&mut buf[0..wire_size]);
        rhw.write(&mut outbuf);
        // wrap the private part in encryption: put an [] at the start and end!
        outbuf.put_u8('[' as u8);
        phw.write(&mut outbuf);
        outbuf.put(pay);
        outbuf.put_u8(']' as u8);
        outbuf.into_inner()
    }

    #[test]
    fn simple_write_works() {
        let mut buf: [u8; 1024];
        unsafe {
            buf = mem::uninitialized();
        }
        let ph = PrivateHeader {
            grant_bytes: 0,
            payload_length: Some(3),
        };
        assert_eq!(
            test_write(
                &mut buf,
                1.into(),
                2.into(),
                StreamId::new(1, StreamType::UnreliableOrdered),
                123.into(),
                ph,
                &[100, 101, 102]
            ),
            [
                // flags -- local
                1,
                // stream id
                (1 << 3) | 2,
                // seq
                237,
                1,
                // private length
                5,
                // private start
                '[' as u8,
                // private header length
                0,
                // payload length
                3,
                // payload
                100,
                101,
                102,
                // private end
                ']' as u8,
            ]
        );
    }

}
