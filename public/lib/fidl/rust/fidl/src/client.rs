// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a client for a fidl interface.

// TODO(cramertj) refactor the `ClientListener` into an `Rc<RefCell>` interface
// inside of `Client` that gets referenced and updated from the future returned
// by `send_msg_expect_response`, rather than a separate, global, everlasting
// future (eww).

use {EncodeBuf, DecodeBuf, MsgType, Error};
use cookiemap::CookieMap;

use std::sync::Arc;

use tokio_core::reactor::Handle;
use tokio_fuchsia;
use futures::{Async, BoxFuture, Future, Poll, Stream};
use futures::sync::oneshot::{self, Canceled};
use futures::sync::mpsc;
use std::io;

type Promise = oneshot::Sender<Result<DecodeBuf, Error>>;

pub struct Client {
    channel: Arc<tokio_fuchsia::Channel>,
    ctrl_tx: mpsc::UnboundedSender<(EncodeBuf, Promise)>,
}

impl Client {
    pub fn new(channel: tokio_fuchsia::Channel, handle: &Handle) -> Client {
        let (ctrl_tx, ctrl_rx) = mpsc::unbounded();
        let channel = Arc::new(channel);
        let listener = ClientListener {
            // TODO: propagate this error
            channel: channel.clone(),
            ctrl_rx,
            pending: CookieMap::new(),
            running: true,
        };
        handle.spawn(listener.map_err(|_| ()));
        Client {
            channel: channel.clone(),
            ctrl_tx,
        }
    }

    pub fn send_msg(&self, buf: &mut EncodeBuf) {
        let (out_buf, handles) = buf.get_mut_content();
        let _ = self.channel.write(out_buf, handles, 0);
    }

    pub fn send_msg_expect_response(&self, buf: EncodeBuf) -> BoxFuture<DecodeBuf, Error> {
        let (tx, rx) = oneshot::channel();
        if let Err(_e) = self.ctrl_tx.send((buf, tx)) {
            // TODO
        }
        Box::new(rx.then(|res| {
            match res {
                Ok(Ok(buf)) => Ok(buf),
                Ok(Err(e)) => Err(e),
                Err(Canceled) => Err(Error::RemoteClosed),
            }
        }))
    }
}

struct ClientListener {
    channel: Arc<tokio_fuchsia::Channel>,
    ctrl_rx: mpsc::UnboundedReceiver<(EncodeBuf, Promise)>,
    running: bool,
    pending: CookieMap<Promise>,
}

impl Future for ClientListener {
    type Item = ();
    type Error = io::Error;
    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        loop {
            let mut buf = DecodeBuf::new();
            match self.channel.recv_from(0, buf.get_mut_buf()) {
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                    if !self.running {
                        return Ok(Async::NotReady);
                    }
                    loop {
                        match self.ctrl_rx.poll().expect("error polling channel") {
                            Async::Ready(None) => {
                                self.running = false;
                                if self.pending.is_empty() {
                                    return Ok(Async::Ready(()));
                                } else {
                                    return Ok(Async::NotReady);
                                }
                            }
                            Async::Ready(Some((mut buf, promise))) => {
                                let id = self.pending.insert(promise);
                                buf.set_message_id(id);
                                let (out_buf, handles) = buf.get_mut_content();
                                let _ = self.channel.write(out_buf, handles, 0);
                                // TODO: handle error on write
                            }
                            Async::NotReady => return Ok(Async::NotReady),
                        }
                    }
                }
                Err(e) => return Err(e),
                Ok(()) => (),
            }

            match buf.decode_message_header() {
                Some(MsgType::Response) => {
                    let id = buf.get_message_id();
                    if let Some(promise) = self.pending.remove(id) {
                        let _ = promise.send(Ok(buf));
                    } else {
                        return Err(io::Error::new(io::ErrorKind::Other, "id not found"));
                    }
                }
                _ => return Err(io::Error::new(io::ErrorKind::Other, "invalid header")),
            }
        }
    }
}

/*
struct ClientListenerState {
    channel: tokio_fuchsia::Channel,
    ctrl_rx: mpsc::Reciever<Event>,
    buf: DecodeBuf,
    pending: CookieMap<Promise>,
}
*/

#[cfg(test)]
mod tests {
    use magenta::{self, MessageBuf, ChannelOpts};
    use std::time::Duration;
    use tokio_fuchsia::Channel;
    use tokio_core::reactor::{Core, Timeout};
    use byteorder::{ByteOrder, LittleEndian};
    use super::*;

    #[test]
    fn client() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();

        let (client_end, server_end) = magenta::Channel::create(ChannelOpts::Normal).unwrap();
        let client_end = Channel::from_channel(client_end, &handle).unwrap();
        let client = Client::new(client_end, &handle);

        let server = Channel::from_channel(server_end, &handle).unwrap();
        let mut buffer = MessageBuf::new();
        let receiver = server.recv_msg(0, &mut buffer).map(|(_chan, buf)| {
            let bytes = &[16, 0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0];
            println!("{:?}", buf.bytes());
            assert_eq!(bytes, buf.bytes());
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let rcv_timeout = Timeout::new(Duration::from_millis(300), &handle).unwrap().map(|()| {
            panic!("did not receive message in time!");
        });
        let receiver = receiver.select(rcv_timeout).map(|(_,_)| ()).map_err(|(err,_)| err);

        let sender = Timeout::new(Duration::from_millis(100), &handle).unwrap().map(|()|{
            let mut req = EncodeBuf::new_request(42);
            client.send_msg(&mut req);
        });

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }

    #[test]
    fn client_with_response() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();

        let (client_end, server_end) = magenta::Channel::create(ChannelOpts::Normal).unwrap();
        let client_end = Channel::from_channel(client_end, &handle).unwrap();
        let client = Client::new(client_end, &handle);

        let server = Channel::from_channel(server_end, &handle).unwrap();
        let mut buffer = MessageBuf::new();
        let receiver = server.recv_msg(0, &mut buffer).map(|(chan, buf)| {
            let bytes = &[24, 0, 0, 0, 1, 0, 0, 0, 42, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0];
            println!("{:?}", buf.bytes());
            assert_eq!(bytes, buf.bytes());
            let id = LittleEndian::read_u64(&buf.bytes()[16..24]);

            let mut response = EncodeBuf::new_response(42);
            response.set_message_id(id);
            let (out_buf, handles) = response.get_mut_content();
            let _ = chan.write(out_buf, handles, 0);
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let rcv_timeout = Timeout::new(Duration::from_millis(300), &handle).unwrap().map(|()| {
            panic!("did not receive message in time!");
        });
        let receiver = receiver.select(rcv_timeout).map(|(_,_)| ()).map_err(|(err,_)| err);

        let req = EncodeBuf::new_request_expecting_response(42);
        let sender = client.send_msg_expect_response(req)
            .map_err(|e| {
                println!("error {:?}", e);
                io::Error::new(io::ErrorKind::Other, "fidl error")
            });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let send_timeout = Timeout::new(Duration::from_millis(300), &handle).unwrap().map(|()| {
            panic!("did not receive response in time!");
        });
        let sender = sender.select(send_timeout).map(|(_,_)| ()).map_err(|(err,_)| err);

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }
}
