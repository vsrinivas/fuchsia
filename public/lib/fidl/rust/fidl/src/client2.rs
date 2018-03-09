// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a client for a fidl interface.

use {async, Error, zircon as zx};
use encoding2::{
    Encodable,
    Decodable,
    Encoder,
    Decoder,
    decode_transaction_header,
    TransactionHeader,
    TransactionMessage
};
use futures::future::{self, Either, FutureResult, AndThen};
use futures::prelude::*;
use futures::task::Waker;
use slab::Slab;
use std::mem;
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicUsize, Ordering};
use self::zx::MessageBuf;

/// A FIDL client which can be used to send buffers and receive responses via a channel.
#[derive(Debug, Clone)]
pub struct Client {
    inner: Arc<ClientInner>,
}

/// A future representing the raw response to a FIDL query.
pub type RawQueryResponseFut = Either<FutureResult<MessageBuf, Error>, MessageResponse>;

/// A future representing the decoded response to a FIDL query.
pub type QueryResponseFut<D> =
    AndThen<
        RawQueryResponseFut,
        Result<D, Error>,
        fn(MessageBuf) -> Result<D, Error>>;

fn decode_from_messagebuf<D: Decodable>(mut buf: MessageBuf) -> Result<D, Error> {
    let (bytes, handles) = buf.split_mut();
    let header_len = <TransactionHeader as Decodable>::inline_size();
    if bytes.len() < header_len { return Err(Error::OutOfRange); }
    let (_header_bytes, body_bytes) = bytes.split_at(header_len);

    let mut output = D::new_empty();
    Decoder::decode_into(body_bytes, handles, &mut output)?;
    Ok(output)
}

fn response_header_tx_id(buf: &[u8]) -> Result<u32, Error> {
    decode_transaction_header(buf).map(|(header, _bytes)| header.tx_id)
}

impl Client {
    /// Create a new client.
    pub fn new(channel: async::Channel) -> Client {
        Client {
            inner: Arc::new(ClientInner {
                channel: channel,
                received_messages_count: AtomicUsize::new(0),
                message_interests: Mutex::new(Slab::new()),
            })
        }
    }

    /// Send an encodable message without expecting a response.
    pub fn send<T: Encodable>(&self, msg: &mut T, ordinal: u32) -> Result<(), Error> {
        let (buf, handles) = (&mut vec![], &mut vec![]);
        let msg = &mut TransactionMessage {
            header: TransactionHeader {
                tx_id: 0,
                flags: 0,
                ordinal,
            },
            body: msg,
        };
        Encoder::encode(buf, handles, msg)?;
        self.send_raw_msg(&**buf, handles)
    }

    /// Send an encodable query and receive a decodable response.
    pub fn send_query<E: Encodable, D: Decodable>(&self, msg: &mut E, ordinal: u32)
        -> QueryResponseFut<D>
    {
        let (buf, handles) = (&mut vec![], &mut vec![]);
        let res_fut = self.send_raw_query(|tx_id| {
            let msg = &mut TransactionMessage {
                header: TransactionHeader {
                    tx_id,
                    flags: 0,
                    ordinal,
                },
                body: msg,
            };
            Encoder::encode(buf, handles, msg)?;
            Ok((buf, handles))
        });

        res_fut.and_then(decode_from_messagebuf::<D>)
    }

    /// Send a raw message without expecting a response.
    pub fn send_raw_msg(&self, buf: &[u8], handles: &mut Vec<zx::Handle>) -> Result<(), Error> {
        Ok(self.inner.channel.write(buf, handles).map_err(Error::ClientWrite)?)
    }

    /// Send a raw query and receive a response future.
    pub fn send_raw_query<'a, F>(&'a self, msg_from_id: F)
            -> RawQueryResponseFut
            where F: FnOnce(u32) -> Result<(&'a mut [u8], &'a mut Vec<zx::Handle>), Error>
    {
        let id = self.inner.register_msg_interest();
        let (out_buf, handles) = match msg_from_id(id) {
            Ok(x) => x,
            Err(e) => return future::err(e).left(),
        };
        if let Err(e) = self.inner.channel.write(out_buf, handles) {
            return future::err(Error::ClientWrite(e)).left();
        }

        MessageResponse {
            id: id,
            client: Some(self.inner.clone()),
            last_registered_waker: None,
        }.right()
    }
}

#[must_use]
/// A future which polls for the response to a client message.
#[derive(Debug)]
pub struct MessageResponse {
    id: u32,
    // `None` if the message response has been recieved
    client: Option<Arc<ClientInner>>,
    last_registered_waker: Option<Waker>,
}

impl Future for MessageResponse {
    type Item = MessageBuf;
    type Error = Error;
    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        let res;
        {
            let client = self.client.as_ref().ok_or(Error::PollAfterCompletion)?;

            let current_waker_is_registered =
                self.last_registered_waker
                    .as_ref()
                    // TODO: re-enable when "PartialEq for Waker" is resolved
                    .map_or(false, |_waker| false/*task.will_notify_current()*/);

            if !current_waker_is_registered {
                let waker = cx.waker();
                res = client.poll_recv(self.id, Some(&waker), cx);
                self.last_registered_waker = Some(waker);
            } else {
                res = client.poll_recv(self.id, None, cx);
            }
        }

        // Drop the client reference if the response has been received
        if let Ok(Async::Ready(_)) = res {
            self.client.take();
        }

        res
    }
}

impl Drop for MessageResponse {
    fn drop(&mut self) {
        if let Some(ref client) = self.client {
            client.deregister_msg_interest(self.id as usize)
        }
    }
}


/// An enum reprenting either a resolved message interest or a task on which to alert
/// that a response message has arrived.
#[derive(Debug)]
enum MessageInterest {
    WillPoll,
    Waiting(Waker),
    Received(MessageBuf),
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

    fn unwrap_received(self) -> MessageBuf {
        if let MessageInterest::Received(buf) = self {
            buf
        } else {
            panic!("EXPECTED received message")
        }
    }
}

/// A shared client channel which tracks EXPECTED and received responses
#[derive(Debug)]
struct ClientInner {
    channel: async::Channel,

    /// The number of `Some` entries in `message_interests`.
    /// This is used to prevent unnecessary locking.
    received_messages_count: AtomicUsize,

    /// A map of message interests to either `None` (no message received yet)
    /// or `Some(DecodeBuf)` when a message has been received.
    /// An interest is registered with `register_msg_interest` and deregistered
    /// by either receiving a message via a call to `poll_recv` or manually
    /// deregistering with `deregister_msg_interest`
    message_interests: Mutex<Slab<MessageInterest>>,
}

impl ClientInner {
    /// Registers interest in a response message.
    ///
    /// This function returns a `usize` ID which should be used to send a message
    /// via the channel. Responses are then received using `poll_recv`.
    fn register_msg_interest(&self) -> u32 {
        // TODO(cramertj) use `try_from` here and assert that the conversion from
        // `usize` to `u32` hasn't overflowed.
        self.message_interests.lock().unwrap().insert(
            MessageInterest::WillPoll) as u32
    }

    /// Check for receipt of a message with a given ID.
    fn poll_recv(
        &self,
        id: u32,
        waker_to_register_opt: Option<&Waker>,
        cx: &mut task::Context
    ) -> Poll<MessageBuf, Error> {
        let id = id as usize;

        // TODO(cramertj) return errors if one has occured _ever_ in poll_recv, not just if
        // one happens on this call.

        // Look to see if there are messages available
        if self.received_messages_count.load(Ordering::AcqRel) > 0 {
            let mut message_interests = self.message_interests.lock().unwrap();

            // If a message was received for the ID in question,
            // remove the message interest entry and return the response.
            if message_interests.get(id).expect("Polled unregistered interest").is_received() {
                let buf = message_interests.remove(id).unwrap_received();
                self.received_messages_count.fetch_sub(1, Ordering::AcqRel);
                return Ok(Async::Ready(buf));
            }
        }

        // Receive messages from the channel until a message with the appropriate ID
        // is found, or the channel is exhausted.
        loop {
            let mut buf = MessageBuf::new();
            if let Async::Pending =
                self.channel.recv_from(&mut buf, cx)
                    .map_err(Error::ClientRead)?
            {
                let mut message_interests = self.message_interests.lock().unwrap();

                if message_interests.get(id)
                    .expect("Polled unregistered interst")
                    .is_received()
                {
                    // If, by happy accident, we just raced to getting the result,
                    // then yay! Return success.
                    let buf = message_interests.remove(id).unwrap_received();
                    self.received_messages_count.fetch_sub(1, Ordering::AcqRel);
                    return Ok(Async::Ready(buf));
                } else {
                    // Set the current waker to be notified when a response arrives.
                    if let Some(waker_to_register) = waker_to_register_opt {
                        *message_interests.get_mut(id)
                            .expect("Polled unregistered interest") =
                                MessageInterest::Waiting(waker_to_register.clone());
                    }
                    return Ok(Async::Pending);
                }
            }

            // TODO(cramertj) handle control messages (e.g. epitaph)
            let recvd_id = response_header_tx_id(buf.bytes()).map_err(|_| Error::InvalidHeader)?;

            // TODO(cramertj) use TryFrom here after stabilization
            let recvd_id = recvd_id as usize;

            // If a message was received for the ID in question,
            // remove the message interest entry and return the response.
            if recvd_id == id {
                self.message_interests.lock().unwrap().remove(id);
                return Ok(Async::Ready(buf));
            }

            // Look for a message interest with the given ID.
            // If one is found, store the message so that it can be picked up later.
            let mut message_interests = self.message_interests.lock().unwrap();
            if let Some(entry) = message_interests.get_mut(recvd_id) {
                let old_entry = mem::replace(entry, MessageInterest::Received(buf));
                self.received_messages_count.fetch_add(1, Ordering::AcqRel);
                if let MessageInterest::Waiting(waker) = old_entry {
                    // Wake up the task to let them know a message has arrived.
                    waker.wake();
                }
            }
        }
    }

    fn deregister_msg_interest(&self, id: usize) {
        if self.message_interests
               .lock().unwrap().remove(id)
               .is_received()
        {
            self.received_messages_count.fetch_sub(1, Ordering::AcqRel);
        }
    }
}

#[cfg(test)]
mod tests {
    use async::{self, TimeoutExt};
    use futures::io;
    use futures::prelude::*;
    use zircon::prelude::*;
    use zircon::{self, MessageBuf};
    use super::*;

    const EXPECTED: &[u8] = &[
        0, 0, 0, 0, 0, 0, 0, 0, // 32 bit tx_id followed by 32 bits of padding
        0, 0, 0, 0, // 32 bits for flags
        42, 0, 0, 0, // 32 bit ordinal
        55, // 8 bit data
        0, 0, 0, 0, 0, 0, 0, // 7 bytes of padding after our 1 byte of data
    ];

    #[test]
    fn client() {
        let mut executor = async::Executor::new().unwrap();

        let (client_end, server_end) = zircon::Channel::create().unwrap();
        let client_end = async::Channel::from_channel(client_end).unwrap();
        let client = Client::new(client_end);

        let server = async::Channel::from_channel(server_end).unwrap();
        let mut buffer = MessageBuf::new();
        let receiver = server.recv_msg(&mut buffer).map(|(_chan, buf)| {
            assert_eq!(EXPECTED, buf.bytes());
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver.on_timeout(
            300.millis().after_now(),
            || panic!("did not receive message in time!")).unwrap();

        let sender = async::Timer::new(100.millis().after_now()).unwrap().map(|()|{
            client.send(&mut 55u8, 42).unwrap();
        });

        let done = receiver.join(sender);
        executor.run_singlethreaded(done).unwrap();
    }

    #[test]
    fn client_with_response() {
        let mut executor = async::Executor::new().unwrap();

        let (client_end, server_end) = zircon::Channel::create().unwrap();
        let client_end = async::Channel::from_channel(client_end).unwrap();
        let client = Client::new(client_end);

        let server = async::Channel::from_channel(server_end).unwrap();
        let mut buffer = MessageBuf::new();
        let receiver = server.recv_msg(&mut buffer).map(|(chan, buf)| {
            assert_eq!(EXPECTED, buf.bytes());
            let id = 0; // internally, the first slot in a slab returns a `0`.

            let response = &mut TransactionMessage {
                header: TransactionHeader {
                    tx_id: id,
                    flags: 0,
                    ordinal: 42,
                },
                body: &mut 55,
            };

            let (bytes, handles) = (&mut vec![], &mut vec![]);
            Encoder::encode(bytes, handles, response).expect("Encoding failure");
            chan.write(bytes, handles).expect("Server channel write failed");
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver.on_timeout(
            300.millis().after_now(),
            || panic!("did not receiver message in time!"
        )).unwrap();

        let sender = client.send_query::<u8, u8>(&mut 55, 42)
            .map(|x|assert_eq!(x, 55))
            .map_err(|e| {
                io::Error::new(
                    io::ErrorKind::Other,
                    &*format!("fidl error: {:?}", e))
            });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let sender = sender.on_timeout(
            300.millis().after_now(),
            || panic!("did not receive response in time!")
        ).unwrap();

        let done = receiver.join(sender.err_into());
        executor.run_singlethreaded(done).unwrap();
    }
}
