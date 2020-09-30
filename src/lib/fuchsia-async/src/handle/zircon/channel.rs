// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::future::Future;
use std::io;
use std::pin::Pin;
use std::task::{Context, Poll};

use fuchsia_zircon::{self as zx, AsHandleRef, MessageBuf};
use futures::ready;

use crate::RWHandle;

/// An I/O object representing a `Channel`.
pub struct Channel(RWHandle<zx::Channel>);

impl AsRef<zx::Channel> for Channel {
    fn as_ref(&self) -> &zx::Channel {
        self.0.get_ref()
    }
}

impl AsHandleRef for Channel {
    fn as_handle_ref(&self) -> zx::HandleRef<'_> {
        self.0.get_ref().as_handle_ref()
    }
}

impl From<Channel> for zx::Channel {
    fn from(channel: Channel) -> zx::Channel {
        channel.0.into_inner()
    }
}

impl Channel {
    /// Creates a new `Channel` from a previously-created `zx::Channel`.
    pub fn from_channel(channel: zx::Channel) -> io::Result<Channel> {
        Ok(Channel(RWHandle::new(channel)?))
    }

    /// Tests to see if the channel received a OBJECT_PEER_CLOSED signal
    pub fn is_closed(&self) -> bool {
        self.0.is_closed()
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `zx::Status::SHOULD_WAIT`.
    ///
    /// Identical to `recv_from` except takes separate bytes and handles buffers
    /// rather than a single `MessageBuf`.
    pub fn read(
        &self,
        cx: &mut Context<'_>,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<zx::Handle>,
    ) -> Poll<Result<(), zx::Status>> {
        let clear_closed = ready!(self.0.poll_read(cx))?;

        let res = self.0.get_ref().read_split(bytes, handles);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.0.need_read(cx, clear_closed)?;
            return Poll::Pending;
        }
        Poll::Ready(res)
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `zx::Status::SHOULD_WAIT`.
    pub fn recv_from(
        &self,
        cx: &mut Context<'_>,
        buf: &mut MessageBuf,
    ) -> Poll<Result<(), zx::Status>> {
        let (bytes, handles) = buf.split_mut();
        self.read(cx, bytes, handles)
    }

    /// Creates a future that receive a message to be written to the buffer
    /// provided.
    ///
    /// The returned future will return after a message has been received on
    /// this socket and been placed into the buffer.
    pub fn recv_msg<'a>(&'a self, buf: &'a mut MessageBuf) -> RecvMsg<'a> {
        RecvMsg { channel: self, buf }
    }

    /// Writes a message into the channel.
    pub fn write(&self, bytes: &[u8], handles: &mut [zx::Handle]) -> Result<(), zx::Status> {
        self.0.get_ref().write(bytes, handles)
    }

    /// Consumes self and returns the underlying zx::Channel
    pub fn into_zx_channel(self) -> zx::Channel {
        self.0.into_inner()
    }
}

impl fmt::Debug for Channel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.get_ref().fmt(f)
    }
}

/// A future used to receive a message from a channel.
///
/// This is created by the `Channel::recv_msg` method.
#[must_use = "futures do nothing unless polled"]
pub struct RecvMsg<'a> {
    channel: &'a Channel,
    buf: &'a mut MessageBuf,
}

impl<'a> Future for RecvMsg<'a> {
    type Output = Result<(), zx::Status>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        this.channel.recv_from(cx, this.buf)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Executor;
    use fuchsia_zircon::{self as zx, MessageBuf};
    use pin_utils::pin_mut;
    use std::mem;

    #[test]
    fn can_receive() {
        let mut exec = Executor::new().unwrap();
        let bytes = &[0, 1, 2, 3];

        let (tx, rx) = zx::Channel::create().unwrap();
        let f_rx = Channel::from_channel(rx).unwrap();

        let receiver = async move {
            let mut buffer = MessageBuf::new();
            f_rx.recv_msg(&mut buffer).await.expect("failed to receive message");
            assert_eq!(bytes, buffer.bytes());
        };
        pin_mut!(receiver);

        assert!(exec.run_until_stalled(&mut receiver).is_pending());

        let mut handles = Vec::new();
        tx.write(bytes, &mut handles).expect("failed to write message");

        assert!(exec.run_until_stalled(&mut receiver).is_ready());
    }

    #[test]
    fn key_reuse() {
        let mut exec = Executor::new().unwrap();
        let (tx0, rx0) = zx::Channel::create().unwrap();
        let (_tx1, rx1) = zx::Channel::create().unwrap();
        let f_rx0 = Channel::from_channel(rx0).unwrap();
        mem::drop(tx0);
        mem::drop(f_rx0);
        let f_rx1 = Channel::from_channel(rx1).unwrap();
        // f_rx0 and f_rx1 use the same key.
        let receiver = async move {
            let mut buffer = MessageBuf::new();
            f_rx1.recv_msg(&mut buffer).await.expect("failed to receive message");
        };
        pin_mut!(receiver);

        assert!(exec.run_until_stalled(&mut receiver).is_pending());
    }
}
