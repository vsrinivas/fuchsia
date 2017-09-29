// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a client for a fidl interface.

use {ClientEnd, ServerEnd, EncodeBuf, DecodeBuf, MsgType, Error};
use cookiemap::CookieMap;

use tokio_core::reactor::Handle;
use tokio_fuchsia;
use futures::{Async, Future, Poll, future, task};
use std::collections::btree_map::Entry;
use std::{io, mem};
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicUsize, Ordering};

/// A FIDL service.
///
/// Implementations of this trait can be used to manufacture instances of a FIDL service
/// and get metadata about a particular service.
pub trait FidlService: Sized {
    /// The type of the structure against which FIDL requests are made.
    /// Queries made against the proxy are sent to the paired `ServerEnd`.
    type Proxy;

    /// Create a new proxy from a `ClientEnd`.
    fn new_proxy(client_end: ClientEnd<Self>, handle: &Handle) -> Result<Self::Proxy, Error>;

    /// Create a new `Proxy`/`ServerEnd` pair.
    fn new_pair(handle: &Handle) -> Result<(Self::Proxy, ServerEnd<Self>), Error>;

    /// The name of the service.
    const NAME: &'static str;

    /// The version of the service.
    const VERSION: u32;
}

/// An enum reprenting either a resolved message interest or a task on which to alert
/// that a response message has arrived.
#[derive(Debug)]
enum MessageInterest {
    WillPoll,
    Waiting(task::Task),
    Received(DecodeBuf),
    Done,
}

impl MessageInterest {
    /// Check if a message has been received.
    fn is_received(&self) -> bool {
        if let MessageInterest::Received(_) = *self {
            true
        } else {
            false
        }
    }

    /// Get the received buffer if one exists.
    fn take_buf(&mut self) -> Option<DecodeBuf> {
        if !self.is_received() {
            // Keep waiting.
            return None;
        }
        let old_self = mem::replace(self, MessageInterest::Done);
        if let MessageInterest::Received(buf) = old_self {
            Some(buf)
        } else {
            None
        }
    }
}

/// A shared client channel which tracks expected and received responses
#[derive(Debug)]
struct ClientInner {
    channel: tokio_fuchsia::Channel,

    /// The number of `Some` entries in `message_interests`.
    /// This is used to prevent unnecessary locking.
    received_messages_count: AtomicUsize,

    /// A map of message interests to either `None` (no message received yet)
    /// or `Some(DecodeBuf)` when a message has been received.
    /// An interest is registered with `register_msg_interest` and deregistered
    /// by either receiving a message via a call to `poll_recv` or manually
    /// deregistering with `deregister_msg_interest`
    message_interests: Mutex<CookieMap<MessageInterest>>,
}

impl ClientInner {
    /// Registers interest in a response message.
    ///
    /// This function returns a `u64` ID which should be used to send a message
    /// via the channel. Responses are then received using `poll_recv`.
    fn register_msg_interest(&self) -> u64 {
        self.message_interests.lock().unwrap().insert(
            MessageInterest::WillPoll)
    }

    /// Check for receipt of a message with a given ID.
    fn poll_recv(&self, id: u64, has_set_task: bool) -> Poll<DecodeBuf, Error> {
        // Look to see if there are messages available
        if self.received_messages_count.load(Ordering::SeqCst) > 0 {
           if let Entry::Occupied(mut entry) =
               self.message_interests.lock().unwrap().inner_map().entry(id)
           {
               // If a message was received for the ID in question,
               // remove the message interest entry and return the response.
               if let Some(buf) = entry.get_mut().take_buf() {
                   entry.remove_entry();
                   self.received_messages_count.fetch_sub(1, Ordering::SeqCst);
                   return Ok(Async::Ready(buf));
               }
           }
        }

        // Receive messages from the channel until a message with the appropriate ID
        // is found, or the channel is exhausted.
        loop {
            let mut buf = DecodeBuf::new();
            match self.channel.recv_from(0, buf.get_mut_buf()) {
                Ok(()) => {},
                Err(e) => {
                    if e.kind() == io::ErrorKind::WouldBlock {
                        // Set the current task to be notified if a response is seen
                        if !has_set_task {
                            if let Entry::Occupied(mut entry) =
                                self.message_interests.lock().unwrap().inner_map().entry(id)
                            {
                                if let Some(buf) = entry.get_mut().take_buf() {
                                    // If, by happy accident, we just raced to getting the result,
                                    // then yay! Return success.
                                    entry.remove_entry();
                                    self.received_messages_count.fetch_sub(1, Ordering::SeqCst);
                                    return Ok(Async::Ready(buf));
                                } else {
                                    // Set the current task to be notified when a response arrives.
                                    *entry.get_mut() = MessageInterest::Waiting(task::current());
                                }
                            }
                        }
                        return Ok(Async::NotReady);
                    } else {
                        return Err(e.into());
                    }
                }
            }

            match buf.decode_message_header() {
                Some(MsgType::Response) => {
                    let recvd_id = buf.get_message_id();

                    // If a message was received for the ID in question,
                    // remove the message interest entry and return the response.
                    if recvd_id == id {
                        self.message_interests.lock().unwrap().remove(id);
                        return Ok(Async::Ready(buf));
                    }

                    // Look for a message interest with the given ID.
                    // If one is found, store the message so that it can be picked up later.
                    if let Entry::Occupied(mut entry) =
                        self.message_interests.lock().unwrap().inner_map().entry(id)
                    {
                        if let MessageInterest::Waiting(ref task) = *entry.get() {
                            // Wake up the task to let them know a message has arrived.
                            task.notify();
                        }
                        *entry.get_mut() = MessageInterest::Received(buf);
                        self.received_messages_count.fetch_add(1, Ordering::SeqCst);
                    }
                }
                _ => {
                    // Ignore messages with invalid headers.
                    // We don't want to stop recieving any messages just because the server
                    // sent one bad one.
                },
            }
        }
    }

    fn deregister_msg_interest(&self, id: u64) {
        if self.message_interests
               .lock().unwrap().remove(id)
               .expect("attempted to deregister unregistered message interest")
               .is_received()
        {
            self.received_messages_count.fetch_sub(1, Ordering::SeqCst);
        }
    }
}

/// A FIDL client which can be used to send buffers and receive responses via a channel.
#[derive(Debug, Clone)]
pub struct Client {
    inner: Arc<ClientInner>,
}

/// A future representing the response to a FIDL message.
pub type SendMessageExpectResponseFuture = future::Either<
        future::FutureResult<DecodeBuf, Error>,
        MessageResponse>;

impl Client {
    /// Create a new client.
    pub fn new(channel: tokio_fuchsia::Channel, handle: &Handle) -> Client {
        // Unused for now-- left in public API to allow for future backwards
        // compatibility and to allow for future changes to spawn futures.
        let _ = handle;
        Client {
            inner: Arc::new(ClientInner {
                channel: channel,
                received_messages_count: AtomicUsize::new(0),
                message_interests: Mutex::new(CookieMap::new()),
            })
        }
    }

    /// Send a message without expecting a response.
    pub fn send_msg(&self, buf: &mut EncodeBuf) -> Result<(), Error> {
        let (out_buf, handles) = buf.get_mut_content();
        Ok(self.inner.channel.write(out_buf, handles, 0)?)
    }

    /// Send a message and receive a response future.
    pub fn send_msg_expect_response(&self, buf: &mut EncodeBuf)
            -> SendMessageExpectResponseFuture
    {
        let id = self.inner.register_msg_interest();
        buf.set_message_id(id);
        let (out_buf, handles) = buf.get_mut_content();
        if let Err(e) = self.inner.channel.write(out_buf, handles, 0) {
            return future::Either::A(future::err(e.into()));
        }

        future::Either::B(MessageResponse {
            id: id,
            client: Some(self.inner.clone()),
            has_polled: false,
        })
    }
}

#[must_use]
/// A future which polls for the response to a client message.
#[derive(Debug)]
pub struct MessageResponse {
    id: u64,
    // `None` if the message response has been recieved
    client: Option<Arc<ClientInner>>,
    has_polled: bool,
}

impl Future for MessageResponse {
    type Item = DecodeBuf;
    type Error = Error;
    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        let res = if let Some(ref client) = self.client {
            client.poll_recv(self.id, self.has_polled)
        } else {
            return Err(Error::PollAfterCompletion)
        };

        // Drop the client reference if the response has been received
        if let Ok(Async::Ready(_)) = res {
            self.client.take();
        }

        self.has_polled = true;

        res
    }
}

impl Drop for MessageResponse {
    fn drop(&mut self) {
        if let Some(ref client) = self.client {
            client.deregister_msg_interest(self.id)
        }
    }
}

#[cfg(test)]
mod tests {
    use zircon::{self, MessageBuf, ChannelOpts};
    use std::time::Duration;
    use tokio_fuchsia::Channel;
    use tokio_core::reactor::{Core, Timeout};
    use byteorder::{ByteOrder, LittleEndian};
    use super::*;

    #[test]
    fn client() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();

        let (client_end, server_end) = zircon::Channel::create(ChannelOpts::Normal).unwrap();
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
            client.send_msg(&mut req).unwrap();
        });

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }

    #[test]
    fn client_with_response() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();

        let (client_end, server_end) = zircon::Channel::create(ChannelOpts::Normal).unwrap();
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

        let mut req = EncodeBuf::new_request_expecting_response(42);
        let sender = client.send_msg_expect_response(&mut req)
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
