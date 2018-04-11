// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::BufMut;

pub struct Writer {
    len: u8,
    val: u64,
}

impl Writer {
    pub fn new(val: u64) -> Writer {
        let len = if val < 1 << 7 {
            1
        } else if val < 1 << 14 {
            2
        } else if val < 1 << 21 {
            3
        } else if val < 1 << 28 {
            4
        } else if val < 1 << 35 {
            5
        } else if val < 1 << 42 {
            6
        } else if val < 1 << 49 {
            7
        } else if val < 1 << 56 {
            8
        } else if val < 1 << 63 {
            9
        } else {
            10
        };

        Writer { len, val }
    }

    pub fn len(&self) -> usize {
        self.len as usize
    }

    pub fn value(&self) -> u64 {
        self.val
    }

    pub fn write<B>(&self, outbuf: &mut B)
    where
        B: BufMut,
    {
        // we know the number of bytes to be written from above
        // here we drop into a sequence of tail-calls depending on length to generate an efficient
        // write routine (this emulates duffs device in rust)
        unsafe {
            assert!(outbuf.bytes_mut().len() >= self.len as usize);
            let p = outbuf.bytes_mut().as_mut_ptr();
            match self.len {
                1 => wrvi1(self.val, p),
                2 => wrvi2(self.val, p),
                3 => wrvi3(self.val, p),
                4 => wrvi4(self.val, p),
                5 => wrvi5(self.val, p),
                6 => wrvi6(self.val, p),
                7 => wrvi7(self.val, p),
                8 => wrvi8(self.val, p),
                9 => wrvi9(self.val, p),
                10 => wrvi10(self.val, p),
                _ => panic!("bad size"),
            }
            *p.offset(self.len as isize - 1) &= 0x7f;
            outbuf.advance_mut(self.len as usize);
        }
    }
}

unsafe fn wrvi1(x: u64, p: *mut u8) {
    *p.offset(0) = (x & 0xff) as u8 | 0x80;
}

unsafe fn wrvi2(x: u64, p: *mut u8) {
    *p.offset(1) = ((x >> 7) & 0xff) as u8 | 0x80;
    wrvi1(x, p);
}

unsafe fn wrvi3(x: u64, p: *mut u8) {
    *p.offset(2) = ((x >> 14) & 0xff) as u8 | 0x80;
    wrvi2(x, p);
}

unsafe fn wrvi4(x: u64, p: *mut u8) {
    *p.offset(3) = ((x >> 21) & 0xff) as u8 | 0x80;
    wrvi3(x, p);
}

unsafe fn wrvi5(x: u64, p: *mut u8) {
    *p.offset(4) = ((x >> 28) & 0xff) as u8 | 0x80;
    wrvi4(x, p);
}

unsafe fn wrvi6(x: u64, p: *mut u8) {
    *p.offset(5) = ((x >> 35) & 0xff) as u8 | 0x80;
    wrvi5(x, p);
}

unsafe fn wrvi7(x: u64, p: *mut u8) {
    *p.offset(6) = ((x >> 42) & 0xff) as u8 | 0x80;
    wrvi6(x, p);
}

unsafe fn wrvi8(x: u64, p: *mut u8) {
    *p.offset(7) = ((x >> 49) & 0xff) as u8 | 0x80;
    wrvi7(x, p);
}

unsafe fn wrvi9(x: u64, p: *mut u8) {
    *p.offset(8) = ((x >> 56) & 0xff) as u8 | 0x80;
    wrvi8(x, p);
}

unsafe fn wrvi10(x: u64, p: *mut u8) {
    *p.offset(9) = ((x >> 63) & 0xff) as u8 | 0x80;
    wrvi9(x, p);
}
