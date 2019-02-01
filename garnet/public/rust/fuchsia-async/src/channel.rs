// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::io;
use std::pin::Pin;

use fuchsia_zircon::{self as zx, AsHandleRef, MessageBuf};
use futures::{task::LocalWaker, try_ready, Future, Poll};

use crate::RWHandle;

/// An I/O object representing a `Channel`.
pub struct Channel(RWHandle<zx::Channel>);

impl AsRef<zx::Channel> for Channel {
    fn as_ref(&self) -> &zx::Channel {
        self.0.get_ref()
    }
}

impl AsHandleRef for Channel {
    fn as_handle_ref(&self) -> zx::HandleRef {
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

    /// Test whether this socket is ready to be read or not.
    ///
    /// If the socket is *not* readable then the current task is scheduled to
    /// get a notification when the socket does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is readable again.
    fn poll_read(&self, lw: &LocalWaker) -> Poll<Result<(), zx::Status>> {
        self.0.poll_read(lw)
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `zx::Status::SHOULD_WAIT`.
    pub fn recv_from(&self, buf: &mut MessageBuf, lw: &LocalWaker) -> Poll<Result<(), zx::Status>> {
        try_ready!(self.poll_read(lw));

        let res = self.0.get_ref().read(buf);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.0.need_read(lw)?;
            return Poll::Pending;
        }
        Poll::Ready(res)
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
    pub fn write(&self, bytes: &[u8], handles: &mut Vec<zx::Handle>) -> Result<(), zx::Status> {
        self.0.get_ref().write(bytes, handles)
    }

    /// Consumes self and returns the underlying zx::Channel
    pub fn into_zx_channel(self) -> zx::Channel {
        self.0.into_inner()
    }
}

impl fmt::Debug for Channel {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
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

    fn poll(mut self: Pin<&mut Self>, lw: &LocalWaker) -> Poll<Self::Output> {
        let this = &mut *self;
        this.channel.recv_from(this.buf, lw)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Executor;
    use fuchsia_zircon::{self as zx, MessageBuf};
    use pin_utils::pin_mut;

    #[test]
    fn can_receive() {
        let mut exec = Executor::new().unwrap();
        let bytes = &[0, 1, 2, 3];

        let (tx, rx) = zx::Channel::create().unwrap();
        let f_rx = Channel::from_channel(rx).unwrap();

        let receiver = async move {
            let mut buffer = MessageBuf::new();
            await!(f_rx.recv_msg(&mut buffer)).expect("failed to receive message");
            assert_eq!(bytes, buffer.bytes());
        };
        pin_mut!(receiver);

        assert!(exec.run_until_stalled(&mut receiver).is_pending());

        let mut handles = Vec::new();
        tx.write(bytes, &mut handles)
            .expect("failed to write message");

        assert!(exec.run_until_stalled(&mut receiver).is_ready());
    }
}
