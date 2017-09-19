// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple fidl server, written in terms of low level messages, and missing
//! error cases.

extern crate zircon;
extern crate mxruntime;

use zircon::{AsHandleRef, Channel, MessageBuf, ZX_TIME_INFINITE, ZX_CHANNEL_READABLE};
use mxruntime::{HandleType, get_startup_handle};

fn align(offset: usize, alignment: usize) -> usize {
    (offset.wrapping_sub(1) | (alignment - 1)).wrapping_add(1)
}

// Real applications should use the byteorder crate instead.
fn write_le32(buf: &mut [u8], val: u32) {
    buf[0] = (val & 0xff) as u8;
    buf[1] = ((val >> 8) & 0xff) as u8;
    buf[2] = ((val >> 16) & 0xff) as u8;
    buf[3] = ((val >> 24) & 0xff) as u8;
}

fn main() {
    let h1 = get_startup_handle(HandleType::OutgoingServices)
        .expect("couldn't get outgoing services handle");
    // wait for ConnectToService request
    let c1 = Channel::from(h1);
    let _ = c1.wait_handle(ZX_CHANNEL_READABLE, ZX_TIME_INFINITE);
    let mut buf = MessageBuf::new();
    let _ = c1.read(0, &mut buf);
    let h2 = buf.take_handle(0).expect("couldn't get service provider handle");
    let c2 = Channel::from(h2);
    let _ = c2.wait_handle(ZX_CHANNEL_READABLE, ZX_TIME_INFINITE);
    let _ = c2.read(0, &mut buf);
    let mut empty = vec![];
    let answer = "hello from Rust";
    let str_len = 8 + align(answer.len(), 8);
    let size = 24 + 8 + 8 + str_len;
    let mut response = vec![0; size];
    write_le32(&mut response[0..4], 24);  // size of data header
    write_le32(&mut response[4..8], 1);  // version for data header
    write_le32(&mut response[8..12], 0);  // type
    write_le32(&mut response[12..16], 2);  // flags, 2 = response
    response[16..24].clone_from_slice(&buf.bytes()[16..24]);  // message id
    write_le32(&mut response[24..28], 16);  // struct header: size
    write_le32(&mut response[28..32], 0);  // struct header: extra
    write_le32(&mut response[32..36], 8);  // offset of string contents
    write_le32(&mut response[36..40], 0);  // offset of string contents (high word)
    write_le32(&mut response[40..44], str_len as u32);  // length of string
    write_le32(&mut response[44..48], answer.len() as u32);  // number of elements in string
    response[48..48 + answer.len()].clone_from_slice(answer.as_bytes());
    let _ = c2.write(&response, &mut empty, 0);
}
