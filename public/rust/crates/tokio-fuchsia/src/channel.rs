// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;
use std::mem;
use std::fmt;
use std::borrow::BorrowMut;

use futures::{Async, Future, Poll};
use mio::fuchsia::{EventedHandle, FuchsiaReady};
use zircon::{self, AsHandleRef, MessageBuf};

use tokio_core::reactor::{Handle, PollEvented};

use super::would_block;

/// An I/O object representing a `Channel`.
pub struct Channel {
    channel: zircon::Channel,
    evented: PollEvented<EventedHandle>,
}

impl AsRef<zircon::Channel> for Channel {
    fn as_ref(&self) -> &zircon::Channel {
        &self.channel
    }
}

impl AsHandleRef for Channel {
    fn as_handle_ref(&self) -> zircon::HandleRef {
        self.channel.as_handle_ref()
    }
}

impl From<Channel> for zircon::Channel {
    fn from(channel: Channel) -> zircon::Channel {
        channel.channel
    }
}

impl Channel {
    /// Creates a new `Channel` from a previously-created `zircon::Channel`.
    pub fn from_channel(channel: zircon::Channel, handle: &Handle) -> io::Result<Channel> {
        // This is safe because the `EventedHandle` will only live as long as the
        // underlying `zircon::Channel`.
        let ev_handle = unsafe { EventedHandle::new(channel.raw_handle()) };
        let evented = PollEvented::new(ev_handle, handle)?;

        Ok(Channel { evented, channel })
    }

    /// Test whether this socket is ready to be read or not.
    ///
    /// If the socket is *not* readable then the current task is scheduled to
    /// get a notification when the socket does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is readable again.
    pub fn poll_read(&self) -> Async<()> {
        self.evented.poll_read()
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `zircon::Status::ErrShouldWait`.
    pub fn recv_from(&self, opts: u32, buf: &mut MessageBuf) -> io::Result<()> {
        let signals = self.evented.poll_ready(FuchsiaReady::from(
                          zircon::ZX_CHANNEL_READABLE |
                          zircon::ZX_CHANNEL_PEER_CLOSED).into());

        match signals {
            Async::NotReady => Err(would_block()),
            Async::Ready(ready) => {
                let signals = FuchsiaReady::from(ready).into_zx_signals();
                if zircon::ZX_CHANNEL_PEER_CLOSED.intersects(signals) {
                    Err(io::ErrorKind::ConnectionAborted.into())
                } else {
                    let res = self.channel.read(opts, buf);
                    if res == Err(zircon::Status::ErrShouldWait) {
                        self.evented.need_read();
                    }
                    res.map_err(io::Error::from)
                }
            }
        }
    }

    /// Creates a future that receive a message to be written to the buffer
    /// provided.
    ///
    /// The returned future will return after a message has been received on
    /// this socket. The future will resolve to the channel and the buffer.
    ///
    /// An error during reading will cause the socket and buffer to get
    /// destroyed and the status will be returned.
    ///
    /// The BorrowMut<MessageBuf> means you can pass either a `MessageBuf`
    /// as well as a `&mut MessageBuf`, as well some other things.
    pub fn recv_msg<T>(self, opts: u32, buf: T) -> RecvMsg<T>
        where T: BorrowMut<MessageBuf>,
    {

        RecvMsg {
            state: RecvState::Reading {
                chan: self,
                buf, opts,
            },
        }
    }

    /// Returns a `Future` that continuously reads messages from the channel
    /// and calls the callback with them, re-using the message buffer. The
    /// callback returns a future that serializes the server loop so it won't
    /// read the next message until the future returns and gives it a
    /// channel and buffer back.
    pub fn chain_server<F,U>(self, opts: u32, callback: F) -> ChainServer<F,U>
        where F: FnMut((Channel, MessageBuf)) -> U,
          U: Future<Item = (Channel, MessageBuf), Error = io::Error>,
    {
        let buf = MessageBuf::new();
        let recv = self.recv_msg(opts, buf);
        let state = ServerState::Waiting(recv);
        ChainServer { callback, opts, state }
    }

    /// Returns a `Future` that continuously reads messages from the channel and
    /// calls the callback with them, re-using the message buffer.
    pub fn repeat_server<F>(self, opts: u32, callback: F) -> RepeatServer<F>
        where F: FnMut(&Channel, &mut MessageBuf)
    {
        let buf = MessageBuf::new();
        RepeatServer { callback, buf, opts, chan: self }
    }

    /// Writes a message into the channel.
    pub fn write(&self,
                 bytes: &[u8],
                 handles: &mut Vec<zircon::Handle>,
                 opts: u32
                ) -> io::Result<()>
    {
        self.channel.write(bytes, handles, opts).map_err(io::Error::from)
    }
}

impl fmt::Debug for Channel {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.channel.fmt(f)
    }
}

/// A future used to receive a message from a channel.
///
/// This is created by the `Channel::recv_msg` method.
#[must_use]
pub struct RecvMsg<T> {
    state: RecvState<T>,
}

enum RecvState<T> {
    Reading {
        chan: Channel,
        buf: T,
        opts: u32,
    },
    Empty,
}

impl<T> Future for RecvMsg<T>
    where T: BorrowMut<MessageBuf>,
{
    type Item = (Channel, T);
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, io::Error> {
        match self.state {
            RecvState::Reading { ref chan, ref mut buf, opts } => {
                try_nb!(chan.recv_from(opts, buf.borrow_mut()));
            }
            RecvState::Empty => panic!("poll a RecvMsg after it's done"),
        };

        match mem::replace(&mut self.state, RecvState::Empty) {
            RecvState::Reading { chan, buf, opts: _ } => {
                Ok(Async::Ready((chan, buf)))
            }
            RecvState::Empty => panic!(),
        }
    }
}

/// Allows repeatedly listening for messages while re-using the message buffer
/// and receiving the channel and buffer for use in the callback.
#[must_use]
pub struct ChainServer<F,U> {
    callback: F,
    opts: u32,
    state: ServerState<U>,
}

enum ServerState<U> {
    Waiting(RecvMsg<MessageBuf>),
    Processing(U),
}

impl<F,U> Future for ChainServer<F,U>
    where F: FnMut((Channel, MessageBuf)) -> U,
          U: Future<Item = (Channel, MessageBuf), Error = io::Error>,
{
    type Item = ();
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        // loop because we might get a new future we have to poll immediately.
        // we only return on error or Async::NotReady
        loop {
            let new_state = match self.state {
                ServerState::Waiting(ref mut recv) => {
                    let chanbuf = try_ready!(recv.poll());
                    // this is needed or else Rust thinks .callback() is a method
                    let ref mut callback = self.callback;
                    let fut = callback(chanbuf);
                    ServerState::Processing(fut)
                },
                ServerState::Processing(ref mut fut) => {
                    let (chan, buf) = try_ready!(fut.poll());
                    let recv = chan.recv_msg(self.opts, buf);
                    ServerState::Waiting(recv)
                }
            };
            let _ = mem::replace(&mut self.state, new_state);
        }
    }
}

/// Allows repeatedly listening for messages while re-using the message buffer.
#[must_use]
pub struct RepeatServer<F> {
    chan: Channel,
    callback: F,
    buf: MessageBuf,
    opts: u32,
}

impl<F> Future for RepeatServer<F>
    where F: FnMut(&Channel, &mut MessageBuf)
{
    type Item = ();
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        loop {
            try_nb!(self.chan.recv_from(self.opts, &mut self.buf));

            // this is needed or else Rust thinks .callback() is a method
            let ref mut callback = self.callback;
            callback(&self.chan, &mut self.buf);
        }
    }
}

#[cfg(test)]
mod tests {
    use tokio_core::reactor::{Core, Timeout};
    use std::time::Duration;
    use zircon::{self, MessageBuf, ChannelOpts};
    use super::*;

    #[test]
    fn can_receive() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();
        let bytes = &[0,1,2,3];

        let (tx, rx) = zircon::Channel::create(ChannelOpts::Normal).unwrap();
        let f_rx = Channel::from_channel(rx, &handle).unwrap();

        let mut buffer = MessageBuf::new();
        let receiver = f_rx.recv_msg(0, &mut buffer).map(|(_chan, buf)| {
            println!("{:?}", buf.bytes());
            assert_eq!(bytes, buf.bytes());
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let rcv_timeout = Timeout::new(Duration::from_millis(300), &handle).unwrap().map(|()| {
            panic!("did not receive message in time!");
        });
        let receiver = receiver.select(rcv_timeout).map(|(_,_)| ()).map_err(|(err,_)| err);

        let sender = Timeout::new(Duration::from_millis(100), &handle).unwrap().map(|()|{
            let mut handles = Vec::new();
            tx.write(bytes, &mut handles, 0).unwrap();
        });

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }

    #[test]
    fn chain_server() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();

        let (tx, rx) = zircon::Channel::create(ChannelOpts::Normal).unwrap();
        let f_rx = Channel::from_channel(rx, &handle).unwrap();

        let mut count = 0;
        let receiver = f_rx.chain_server(0, |(chan, buf)| {
            println!("{}: {:?}", count, buf.bytes());
            assert_eq!(1, buf.bytes().len());
            assert_eq!(count, buf.bytes()[0]);
            count += 1;
            Timeout::new(Duration::from_millis(10), &handle).unwrap().map(move |()| (chan, buf))
        });

        // add a timeout to receiver to stop the server eventually
        let rcv_timeout = Timeout::new(Duration::from_millis(400), &handle).unwrap();
        let receiver = receiver.select(rcv_timeout).map(|(_,_)| ()).map_err(|(err,_)| err);

        let sender = Timeout::new(Duration::from_millis(100), &handle).unwrap().map(|()|{
            let mut handles = Vec::new();
            tx.write(&[0], &mut handles, 0).unwrap();
            tx.write(&[1], &mut handles, 0).unwrap();
            tx.write(&[2], &mut handles, 0).unwrap();
        });

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }


    #[test]
    fn repeat_server() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();

        let (tx, rx) = zircon::Channel::create(ChannelOpts::Normal).unwrap();
        let f_rx = Channel::from_channel(rx, &handle).unwrap();

        let mut count = 0;
        let receiver = f_rx.repeat_server(0, |_chan, buf| {
            println!("{}: {:?}", count, buf.bytes());
            assert_eq!(1, buf.bytes().len());
            assert_eq!(count, buf.bytes()[0]);
            count += 1;
        });

        // add a timeout to receiver to stop the server eventually
        let rcv_timeout = Timeout::new(Duration::from_millis(400), &handle).unwrap();
        let receiver = receiver.select(rcv_timeout).map(|(_,_)| ()).map_err(|(err,_)| err);

        let sender = Timeout::new(Duration::from_millis(100), &handle).unwrap().map(|()|{
            let mut handles = Vec::new();
            tx.write(&[0], &mut handles, 0).unwrap();
            tx.write(&[1], &mut handles, 0).unwrap();
            tx.write(&[2], &mut handles, 0).unwrap();
        });

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }
}
