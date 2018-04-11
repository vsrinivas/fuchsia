// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::BufMut;
use std::cmp;
use std::mem;
use varint;

// Sequence numbers
#[derive(Clone, Debug)]
pub enum SequenceNum {
    Fork(u64, Vec<u8>),
    AcceptIntro(Vec<u8>),
    Num(u64),
}

impl SequenceNum {
    pub fn new_accept_intro(accept_data: Vec<u8>) -> SequenceNum {
        SequenceNum::AcceptIntro(accept_data)
    }

    pub fn diff(&self, other: &SequenceNum) -> i64 {
        use sequence_num::SequenceNum::*;
        match (self, other) {
            (&Num(a), &Num(b)) => a as i64 - b as i64,
            (&Num(a), _) => a as i64,
            (_, &Num(b)) => -(b as i64),
            (_, _) => 0,
        }
    }

    pub fn next(&mut self) -> SequenceNum {
        let tmp = self.inc(1);
        mem::replace(self, tmp)
    }

    pub fn inc(&self, amt: u64) -> SequenceNum {
        use sequence_num::SequenceNum::Num;
        match (self, amt) {
            (&Num(n), _) => Num(n + amt),
            (v, 0) => v.clone(),
            (_, _) => Num(amt),
        }
    }

    pub fn index(&self) -> u64 {
        use sequence_num::SequenceNum::Num;
        match self {
            &Num(n) => n,
            _ => 0,
        }
    }

    pub fn writer<'a, C>(&'a self, ctx: &C) -> Writer<'a>
    where
        C: SequenceSerCtx,
    {
        use sequence_num::SequenceNum::*;
        let (fst, lst) = ctx.window();
        debug_assert!(*self >= fst);
        debug_assert!(*self <= lst);
        let outstanding = lst.diff(&fst) + 1;
        let width = if outstanding < 1 << 4 {
            1
        } else if outstanding < 1 << 12 {
            2
        } else if outstanding < 1 << 20 {
            3
        } else if outstanding < 1 << 28 {
            4
        } else {
            panic!("Too many outstanding messages")
        };
        match self {
            &Num(seq) => Writer {
                seq: seq,
                width,
                extra: WriteExtra::Nothing,
            },
            &Fork(stream, ref buf) => Writer {
                seq: 0,
                width,
                extra: WriteExtra::Fork(
                    varint::Writer::new(stream),
                    varint::Writer::new(buf.len() as u64),
                    buf,
                ),
            },
            &AcceptIntro(ref buf) => Writer {
                seq: 0,
                width,
                extra: WriteExtra::AcceptIntro(varint::Writer::new(buf.len() as u64), buf),
            },
        }
    }
}

impl PartialEq for SequenceNum {
    fn eq(&self, other: &Self) -> bool {
        use sequence_num::SequenceNum::*;
        match (self, other) {
            (&Num(a), &Num(b)) => a == b,
            (&Num(_), _) => false,
            (_, &Num(_)) => false,
            (&Fork(ref a, ref p), &Fork(ref b, ref q)) => a == b && p == q,
            (&AcceptIntro(ref a), &AcceptIntro(ref b)) => a == b,
            _ => false,
        }
    }
}

impl PartialOrd for SequenceNum {
    fn partial_cmp(&self, other: &Self) -> Option<cmp::Ordering> {
        use sequence_num::SequenceNum::*;
        match (self, other) {
            (&Num(a), &Num(b)) => a.partial_cmp(&b),
            (&Num(a), _) => a.partial_cmp(&0),
            (_, &Num(b)) => 0.partial_cmp(&b),
            (&Fork(ref a, ref p), &Fork(ref b, ref q)) if a == b && p == q => {
                Some(cmp::Ordering::Equal)
            }
            (&AcceptIntro(ref a), &AcceptIntro(ref b)) if a == b => Some(cmp::Ordering::Equal),
            (_, _) => None,
        }
    }
}

pub trait SequenceSerCtx {
    fn window(&self) -> (SequenceNum, SequenceNum);
}

enum WriteExtra<'a> {
    Nothing,
    Fork(varint::Writer, varint::Writer, &'a [u8]),
    AcceptIntro(varint::Writer, &'a [u8]),
}

pub struct Writer<'a> {
    seq: u64,
    width: u8,
    extra: WriteExtra<'a>,
}

impl<'a> Writer<'a> {
    pub fn len(&self) -> usize {
        self.width as usize + match self.extra {
            WriteExtra::Nothing => 0,
            WriteExtra::Fork(ref w1, ref w2, ref b) => 1 + w1.len() + w2.len() + b.len(),
            WriteExtra::AcceptIntro(ref w, ref b) => 1 + w.len() + b.len(),
        }
    }

    pub fn write<B>(&self, buf: &mut B)
    where
        B: BufMut,
    {
        let mut s = self.seq;
        buf.put_u8((((s & 0x3f) << 2) as u8) | (self.width - 1));
        s >>= 6;
        for _ in 1..self.width {
            buf.put_u8((s & 0xff) as u8);
            s >>= 8;
        }
        match self.extra {
            WriteExtra::Nothing => {}
            WriteExtra::Fork(ref w1, ref w2, ref b) => {
                buf.put_u8(0);
                w1.write(buf);
                w2.write(buf);
                buf.put_slice(b);
            }
            WriteExtra::AcceptIntro(ref w, ref b) => {
                buf.put_u8(1);
                w.write(buf);
                buf.put_slice(b);
            }
        }
    }
}

impl From<u64> for SequenceNum {
    fn from(seq: u64) -> SequenceNum {
        debug_assert!(seq > 0);
        SequenceNum::Num(seq)
    }
}
