// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A wrapper to expose Zircon kernel objects for use in tokio.

#![deny(missing_docs)]

extern crate bytes;
#[macro_use]
extern crate futures;
#[macro_use]
extern crate tokio_core;
extern crate tokio_io;
extern crate mio;
extern crate fuchsia_zircon as zircon;

mod channel;
mod socket;

pub use channel::Channel;
pub use socket::Socket;

use std::io;

use bytes::{BufMut, BytesMut};
use tokio_io::codec::{Encoder, Decoder};

/// A simple `Codec` implementation that just ships bytes around.
///
/// This type is used for "framing" a TCP stream of bytes but it's really
/// just a convenient method for us to work with streams/sinks for now.
/// This'll just take any data read and interpret it as a "frame" and
/// conversely just shove data into the output location without looking at
/// it.
//
// TODO(cramertj):
// replace with one from tokio-io pending https://github.com/tokio-rs/tokio-io/issues/75
pub struct Bytes;

impl Decoder for Bytes {
    type Item = BytesMut;
    type Error = io::Error;

    fn decode(&mut self, buf: &mut BytesMut) -> io::Result<Option<BytesMut>> {
        if buf.len() > 0 {
            let len = buf.len();
            Ok(Some(buf.split_to(len)))
        } else {
            Ok(None)
        }
    }

    fn decode_eof(&mut self, buf: &mut BytesMut) -> io::Result<Option<BytesMut>> {
        self.decode(buf)
    }
}

impl Encoder for Bytes {
    type Item = Vec<u8>;
    type Error = io::Error;

    fn encode(&mut self, data: Vec<u8>, buf: &mut BytesMut) -> io::Result<()> {
        buf.put(&data[..]);
        Ok(())
    }
}


#[inline(always)]
fn would_block() -> io::Error {
    io::Error::from(io::ErrorKind::WouldBlock)
}
