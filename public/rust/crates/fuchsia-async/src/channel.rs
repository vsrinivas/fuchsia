// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;
use std::mem;
use std::fmt;
use std::borrow::BorrowMut;

use futures::{Async, IntoFuture, Future, Poll, task};
use zx::{self, AsHandleRef, MessageBuf};

use RWHandle;

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
    fn poll_read(&self, cx: &mut task::Context) -> Poll<(), zx::Status> {
        self.0.poll_read(cx)
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `zx::Status::SHOULD_WAIT`.
    pub fn recv_from(&self, buf: &mut MessageBuf, cx: &mut task::Context)
        -> Poll<(), zx::Status>
    {
        try_ready!(self.poll_read(cx));

        let res = self.0.get_ref().read(buf);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.0.need_read(cx)?;
            return Ok(Async::Pending);
        }
        res.map(Async::Ready)
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
    pub fn recv_msg<T>(self, buf: T) -> RecvMsg<T>
        where T: BorrowMut<MessageBuf>,
    {
        RecvMsg(Some((self, buf)))
    }

    /// Returns a `Future` that continuously reads messages from the channel
    /// and calls the callback with them, re-using the message buffer. The
    /// callback returns a future that serializes the server loop so it won't
    /// read the next message until the future returns and gives it a
    /// channel and buffer back.
    pub fn chain_server<F,U>(self, callback: F) -> ChainServer<F,U>
        where F: FnMut((Channel, MessageBuf)) -> U,
              U: IntoFuture<Item = (Channel, MessageBuf), Error = zx::Status>,
    {
        let buf = MessageBuf::new();
        let recv = self.recv_msg(buf);
        let state = ServerState::Waiting(recv);
        ChainServer { callback, state }
    }

    /// Returns a `Future` that continuously reads messages from the channel and
    /// calls the callback with them, re-using the message buffer.
    pub fn repeat_server<F>(self, callback: F) -> RepeatServer<F>
        where F: FnMut(&Channel, &mut MessageBuf)
    {
        let buf = MessageBuf::new();
        RepeatServer { callback, buf, chan: self }
    }

    /// Writes a message into the channel.
    pub fn write(&self,
                 bytes: &[u8],
                 handles: &mut Vec<zx::Handle>,
                ) -> Result<(), zx::Status>
    {
        self.0.get_ref().write(bytes, handles)
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
pub struct RecvMsg<T>(Option<(Channel, T)>);

impl<T> Future for RecvMsg<T>
    where T: BorrowMut<MessageBuf>,
{
    type Item = (Channel, T);
    type Error = zx::Status;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        {
            let (chan, buf) = self.0.as_mut().expect("polled a RecvMsg after completion");
            try_ready!(chan.recv_from(buf.borrow_mut(), cx));
        }
        let (chan, buf) = self.0.take().unwrap();
        Ok(Async::Ready((chan, buf)))
    }
}

/// Allows repeatedly listening for messages while re-using the message buffer
/// and receiving the channel and buffer for use in the callback.
#[must_use = "futures do nothing unless polled"]
pub struct ChainServer<F,U: IntoFuture> {
    callback: F,
    state: ServerState<U>,
}

enum ServerState<U: IntoFuture> {
    Waiting(RecvMsg<MessageBuf>),
    Processing(U::Future),
}

impl<F,U> Future for ChainServer<F,U>
    where F: FnMut((Channel, MessageBuf)) -> U,
          U: IntoFuture<Item = (Channel, MessageBuf), Error = zx::Status>,
{
    type Item = ();
    type Error = U::Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        // loop because we might get a new future we have to poll immediately.
        // we only return on error or Async::Pending
        loop {
            let new_state = match &mut self.state {
                ServerState::Waiting(recv) => {
                    let chanbuf = try_ready!(recv.poll(cx));
                    let fut = (self.callback)(chanbuf).into_future();
                    ServerState::Processing(fut)
                },
                ServerState::Processing(fut) => {
                    let (chan, buf) = try_ready!(fut.poll(cx));
                    let recv = chan.recv_msg(buf);
                    ServerState::Waiting(recv)
                }
            };
            let _ = mem::replace(&mut self.state, new_state);
        }
    }
}

/// Allows repeatedly listening for messages while re-using the message buffer.
#[must_use = "futures do nothing unless polled"]
pub struct RepeatServer<F> {
    chan: Channel,
    callback: F,
    buf: MessageBuf,
}

impl<F> Future for RepeatServer<F>
    where F: FnMut(&Channel, &mut MessageBuf)
{
    type Item = ();
    type Error = io::Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        loop {
            try_ready!(self.chan.recv_from(&mut self.buf, cx));

            (self.callback)(&self.chan, &mut self.buf);
            self.buf.clear();
        }
    }
}

#[cfg(test)]
mod tests {
    use {Executor, Timer, TimeoutExt};
    use zx::prelude::*;
    use zx::{self, MessageBuf};
    use futures::prelude::*;
    use futures::channel::oneshot;
    use futures::executor::Executor as FutureExecutor;
    use futures::future;
    use super::*;

    #[test]
    fn can_receive() {
        let mut exec = Executor::new().unwrap();
        let bytes = &[0,1,2,3];

        let (tx, rx) = zx::Channel::create().unwrap();
        let f_rx = Channel::from_channel(rx).unwrap();

        let mut buffer = MessageBuf::new();
        let receiver = f_rx.recv_msg(&mut buffer).map(|(_chan, buf)| {
            println!("{:?}", buf.bytes());
            assert_eq!(bytes, buf.bytes());
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver.on_timeout(
                        1000.millis().after_now(),
                        || panic!("timeout")).unwrap();

        let sender = Timer::new(100.millis().after_now()).map(|()| {
            let mut handles = Vec::new();
            tx.write(bytes, &mut handles).unwrap();
        });

        let done = receiver.join(sender);
        exec.run_singlethreaded(done).unwrap();
    }

    #[test]
    fn chain_server() {
        let mut exec = Executor::new().unwrap();

        let (tx, rx) = zx::Channel::create().unwrap();
        let f_rx = Channel::from_channel(rx).unwrap();

        let mut count = 0;
        let receiver = f_rx.chain_server(|(chan, buf)| {
            println!("got bytes: {}: {:?}", count, buf.bytes());
            assert_eq!(1, buf.bytes().len());
            assert_eq!(count, buf.bytes()[0]);
            count += 1;
            Timer::new(100.millis().after_now())
                .map(move |()| (chan, buf))
        });

        // add a timeout to receiver to stop the server eventually
        let receiver = receiver.on_timeout(400.millis().after_now(), || Ok(())).unwrap();

        let sender = Timer::new(100.millis().after_now()).map(|()|{
            let mut handles = Vec::new();
            tx.write(&[0], &mut handles).unwrap();
            tx.write(&[1], &mut handles).unwrap();
            tx.write(&[2], &mut handles).unwrap();
        });

        let done = receiver.join(sender);
        exec.run_singlethreaded(done).unwrap();
    }

    #[test]
    fn chain_server_pre_write() {
        let mut exec = Executor::new().unwrap();
        let mut ehandle = exec.ehandle();

        let (tx, rx) = zx::Channel::create().unwrap();
        tx.write(b"txidhelloworld", &mut vec![]).unwrap();

        let f_rx = Channel::from_channel(rx).unwrap();
        let (completer, completion) = oneshot::channel();

        let mut maybe_completer = Some(completer);

        let receiver = f_rx.chain_server(move |(chan, buf)| {
            maybe_completer.take().unwrap().send(buf.bytes().to_owned()).unwrap();
            future::ok((chan, buf))
        });
        ehandle.spawn(Box::new(receiver.recover(|e| println!("{:?}", e)))).unwrap();

        let mut got_result = false;
        exec.run_singlethreaded(completion.map(|b| {
            assert_eq!(b"txidhelloworld".to_vec(), b);
            got_result = true;
        } ).map_err(|e| {
            assert!(false, format!("unexpected error {:?}", e))
        })).unwrap();

        assert!(got_result);
    }

    #[test]
    fn repeat_server() {
        let mut exec = Executor::new().unwrap();

        let (tx, rx) = zx::Channel::create().unwrap();
        let f_rx = Channel::from_channel(rx).unwrap();

        let mut count = 0;
        let receiver = f_rx.repeat_server(|_chan, buf| {
            println!("{}: {:?}", count, buf.bytes());
            assert_eq!(1, buf.bytes().len());
            assert_eq!(count, buf.bytes()[0]);
            count += 1;
        });

        // add a timeout to receiver to stop the server eventually
        let receiver = receiver.on_timeout(500.millis().after_now(), || Ok(())).unwrap();

        let sender = Timer::new(100.millis().after_now()).map(|()|{
            let mut handles = Vec::new();
            tx.write(&[0], &mut handles).unwrap();
            tx.write(&[1], &mut handles).unwrap();
            tx.write(&[2], &mut handles).unwrap();
        });

        let done = receiver.join(sender);
        exec.run_singlethreaded(done).unwrap();
    }
}
