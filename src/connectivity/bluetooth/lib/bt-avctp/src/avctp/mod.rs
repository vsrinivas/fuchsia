// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_bluetooth::types::Channel,
    fuchsia_zircon as zx,
    futures::{
        ready,
        stream::{FusedStream, Stream},
        task::{Context, Poll, Waker},
    },
    log::{info, trace, warn},
    parking_lot::Mutex,
    slab::Slab,
    std::{collections::VecDeque, convert::TryFrom, marker::Unpin, mem, pin::Pin, sync::Arc},
};

#[cfg(test)]
mod tests;

mod types;

use crate::{Decodable, Encodable, Error, Result};

use self::types::AV_REMOTE_PROFILE;

pub use self::types::{Header, MessageType, PacketType, TxLabel};

/// Peer connection to a remote device uses AVCTP protocol over an L2CAP socket. Used by the AVC
/// peer that encapsulates this peer connection and directly in AVRCP for non AV\C connections like
/// the browse channel.
#[derive(Debug)]
pub struct Peer {
    inner: Arc<PeerInner>,
}

#[derive(Debug)]
struct PeerInner {
    /// Channel to the remote device owned by this peer object.
    channel: Channel,

    /// A map of transaction ids that have been sent but the response has not
    /// been received and/or processed yet.
    ///
    /// Waiters are added with `add_response_waiter` and get removed when they are
    /// polled or they are removed with `remove_waiter`
    response_waiters: Mutex<Slab<ResponseWaiter>>,

    /// A queue of requests that have been received and are waiting to
    /// be responded to, along with the waker for the task that has
    /// taken the request receiver (if it exists)
    incoming_requests: Mutex<CommandQueue>,
}

impl Peer {
    /// Create a new peer object from a established L2CAP socket with the peer.
    pub fn new(channel: Channel) -> Self {
        Self { inner: Arc::new(PeerInner::new(channel)) }
    }

    /// Returns a stream of incoming commands from a remote peer.
    /// Stream returns Command objects on success that can be used to send back responses.
    pub fn take_command_stream(&self) -> CommandStream {
        {
            let mut lock = self.inner.incoming_requests.lock();
            if let CommandListener::None = lock.listener {
                lock.listener = CommandListener::New;
            } else {
                panic!("Command stream has already been taken");
            }
        }

        CommandStream { inner: self.inner.clone() }
    }

    /// Send an outgoing command to the remote peer. Returns a CommandResponseStream to
    /// handle incoming response packets.
    pub fn send_command(&self, payload: &[u8]) -> Result<CommandResponseStream> {
        let id = self.inner.add_response_waiter()?;
        let avctp_header = Header::new(id, AV_REMOTE_PROFILE.clone(), MessageType::Command, false);
        {
            self.inner.send_packet(&avctp_header, payload)?;
        }

        Ok(CommandResponseStream::new(avctp_header.label().clone(), self.inner.clone()))
    }
}

impl PeerInner {
    fn new(channel: Channel) -> Self {
        Self {
            channel,
            response_waiters: Mutex::new(Slab::<ResponseWaiter>::new()),
            incoming_requests: Mutex::<CommandQueue>::default(),
        }
    }

    /// Add a response waiter, and return a id that can be used to send the
    /// transaction.  Responses then can be received using poll_recv_response
    fn add_response_waiter(&self) -> Result<TxLabel> {
        let key = self.response_waiters.lock().insert(ResponseWaiter::default());
        let id = TxLabel::try_from(key as u8);
        if id.is_err() {
            warn!("Transaction IDs are exhausted");
            self.response_waiters.lock().remove(key);
        }
        id
    }

    /// When a waiter isn't interested in the response anymore, we need to just
    /// throw it out.  This is called when the response future is dropped.
    fn remove_response_interest(&self, id: &TxLabel) {
        let mut lock = self.response_waiters.lock();
        let idx = usize::from(id);
        lock.remove(idx);
    }

    /// Attempts to receive a new request by processing all packets on the socket.
    /// Resolves to an unprocessed request (header, body) if one was received.
    /// Resolves to an error if there was an error reading from the socket or if the peer
    /// disconnected.
    fn poll_recv_request(&self, cx: &mut Context<'_>) -> Poll<Result<Packet>> {
        let is_closed = self.recv_all(cx)?;

        let mut lock = self.incoming_requests.lock();

        match lock.queue.pop_front() {
            Some(request) => Poll::Ready(Ok(request)),
            _ => {
                if is_closed {
                    Poll::Ready(Err(Error::PeerDisconnected))
                } else {
                    // Set the waker to be notified when a command shows up.
                    lock.listener = CommandListener::Some(cx.waker().clone());
                    Poll::Pending
                }
            }
        }
    }

    /// Attempts to receive a response to a request by processing all packets on the socket.
    /// Resolves to the bytes in the response body if one was received.
    /// Resolves to an error if there was an error reading from the socket, if the peer
    /// disconnected, or if the |label| is not being waited on.
    fn poll_recv_response(&self, label: &TxLabel, cx: &mut Context<'_>) -> Poll<Result<Packet>> {
        let is_closed = self.recv_all(cx)?;

        let mut waiters = self.response_waiters.lock();
        let idx = usize::from(label);
        // We expect() below because the label above came from an internally-created object,
        // so the waiters should always exist in the map.
        let waiter = waiters.get_mut(idx).expect("Polled unregistered waiter");
        if waiter.has_response() {
            // We got our response.
            let packet = waiter.pop_received();
            Poll::Ready(Ok(packet))
        } else {
            if is_closed {
                Poll::Ready(Err(Error::PeerDisconnected))
            } else {
                // Set the waker to be notified when a response shows up.
                waiter.listener = ResponseListener::Some(cx.waker().clone());
                Poll::Pending
            }
        }
    }

    /// Poll for any packets on the socket
    /// Returns whether the channel was closed, or an Error::PeerRead or Error::PeerWrite
    /// if there was a problem communicating on the socket.
    fn recv_all(&self, cx: &mut Context<'_>) -> Result<bool> {
        let mut buf = Vec::<u8>::new();
        loop {
            let packet_size = match self.channel.poll_datagram(cx, &mut buf) {
                Poll::Ready(Err(zx::Status::PEER_CLOSED)) => {
                    trace!("Peer closed");
                    return Ok(true);
                }
                Poll::Ready(Err(e)) => return Err(Error::PeerRead(e)),
                Poll::Pending => return Ok(false),
                Poll::Ready(Ok(size)) => size,
            };
            if packet_size == 0 {
                continue;
            }
            trace!("received packet {:?}", buf);
            // Detects General Reject condition and sends the response back.
            // On other headers with errors, sends BAD_HEADER to the peer
            // and attempts to continue.
            let avctp_header = match Header::decode(buf.as_slice()) {
                Err(_) => {
                    // Only possible error is OutOfRange
                    // Returned only when the packet is too small, can't make a meaningful reject.
                    info!("received unrejectable message");
                    buf = buf.split_off(packet_size);
                    continue;
                }
                Ok(x) => x,
            };

            // We only support AV remote targeted AVCTP messages on this socket.
            // Send a rejection AVCTP messages with invalid profile id bit set to true.
            if avctp_header.profile_id() != AV_REMOTE_PROFILE {
                info!("received packet not targeted at remote profile service class");
                let resp_avct = avctp_header.create_invalid_profile_id_response();
                self.send_packet(&resp_avct, &[])?;
                buf = buf.split_off(packet_size);
                continue;
            }

            if packet_size == avctp_header.encoded_len() {
                // Only the avctp header was sent with no payload.
                info!("received incomplete packet");
                buf = buf.split_off(packet_size);
                continue;
            }

            let rest = buf.split_off(packet_size);
            let body = buf.split_off(avctp_header.encoded_len());
            // Commands from the remote get translated into requests.
            match avctp_header.message_type() {
                MessageType::Command => {
                    let mut lock = self.incoming_requests.lock();
                    lock.queue.push_back(Packet { header: avctp_header, body: body.to_vec() });
                    if let CommandListener::Some(ref waker) = lock.listener {
                        waker.wake_by_ref();
                    }
                    buf = rest;
                }
                MessageType::Response => {
                    // Should be a response to a command we sent.
                    let mut waiters = self.response_waiters.lock();
                    let idx = usize::from(avctp_header.label());

                    if let Some(waiter) = waiters.get_mut(idx) {
                        waiter
                            .queue
                            .push_back(Packet { header: avctp_header, body: body.to_vec() });
                        let old_entry = mem::replace(&mut waiter.listener, ResponseListener::New);
                        if let ResponseListener::Some(waker) = old_entry {
                            waker.wake();
                        }
                    } else {
                        trace!("response for {:?} we did not send, dropping", avctp_header.label());
                    };
                    buf = rest;
                    // Note: we drop any TxLabel response we are not waiting for
                }
            }
        }
    }

    // Wakes up an arbitrary task that has begun polling on the channel so that
    // it will call recv_all and be registered as the new channel reader.
    fn wake_any(&self) {
        // Try to wake up response waiters first, rather than the event listener.
        // The event listener is a stream, and so could be between poll_nexts,
        // Response waiters should always be actively polled once
        // they've begun being polled on a task.
        {
            let lock = self.response_waiters.lock();
            for (_, response_waiter) in lock.iter() {
                if let ResponseListener::Some(ref waker) = response_waiter.listener {
                    waker.wake_by_ref();
                    return;
                }
            }
        }
        {
            let lock = self.incoming_requests.lock();
            if let CommandListener::Some(ref waker) = lock.listener {
                waker.wake_by_ref();
                return;
            }
        }
    }

    pub fn send_packet(&self, resp_header: &Header, body: &[u8]) -> Result<()> {
        let mut rbuf = vec![0 as u8; resp_header.encoded_len()];
        resp_header.encode(&mut rbuf)?;
        if body.len() > 0 {
            rbuf.extend_from_slice(body);
        }
        self.channel.as_ref().write(rbuf.as_slice()).map_err(|x| Error::PeerWrite(x))?;
        Ok(())
    }
}

/// A stream of requests from the remote peer.
#[derive(Debug)]
pub struct CommandStream {
    inner: Arc<PeerInner>,
}

impl Unpin for CommandStream {}

impl Stream for CommandStream {
    type Item = Result<Command>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        Poll::Ready(match ready!(self.inner.poll_recv_request(cx)) {
            Ok(Packet { header, body, .. }) => {
                Some(Ok(Command { peer: self.inner.clone(), avctp_header: header, body: body }))
            }
            Err(Error::PeerDisconnected) => None,
            Err(e) => Some(Err(e)),
        })
    }
}

impl Drop for CommandStream {
    fn drop(&mut self) {
        self.inner.incoming_requests.lock().listener = CommandListener::None;
        self.inner.wake_any();
    }
}

#[derive(Debug)]
pub struct Command {
    peer: Arc<PeerInner>,
    avctp_header: Header,
    body: Vec<u8>,
}

impl Command {
    pub fn header(&self) -> &Header {
        &self.avctp_header
    }

    pub fn body(&self) -> &[u8] {
        &self.body[..]
    }

    pub fn send_response(&self, body: &[u8]) -> Result<()> {
        let response_header = self.avctp_header.create_response(PacketType::Single);
        self.peer.send_packet(&response_header, body)
    }
}

#[derive(Debug)]
pub struct Packet {
    header: Header,
    body: Vec<u8>,
}

impl Packet {
    pub fn header(&self) -> &Header {
        &self.header
    }

    pub fn body(&self) -> &[u8] {
        &self.body[..]
    }
}

#[derive(Debug, Default)]
struct CommandQueue {
    listener: CommandListener,
    queue: VecDeque<Packet>,
}

#[derive(Debug)]
enum CommandListener {
    /// No one is listening.
    None,
    /// Someone wants to listen but hasn't polled.
    New,
    /// Someone is listening, and can be woken with the waker.
    Some(Waker),
}

impl Default for CommandListener {
    fn default() -> Self {
        CommandListener::None
    }
}

#[derive(Debug, Default)]
struct ResponseWaiter {
    listener: ResponseListener,
    queue: VecDeque<Packet>,
}

/// An enum representing an interest in the response to a command.
#[derive(Debug)]
enum ResponseListener {
    /// A new waiter which hasn't been polled yet.
    New,
    /// A task waiting for a response, which can be woken with the waker.
    Some(Waker),
}

impl Default for ResponseListener {
    fn default() -> Self {
        ResponseListener::New
    }
}

impl ResponseWaiter {
    /// Check if a message has been received.
    fn has_response(&self) -> bool {
        !self.queue.is_empty()
    }

    fn pop_received(&mut self) -> Packet {
        if !self.has_response() {
            panic!("expected received buf");
        }
        self.queue.pop_front().expect("response listener packet queue is unexpectedly empty")
    }
}

/// A stream wrapper that polls for the responses to a command we sent.
/// Removes the associated response waiter when dropped or explicitly
/// completed.
#[derive(Debug)]
pub struct CommandResponseStream {
    id: Option<TxLabel>,
    inner: Arc<PeerInner>,
    done: bool,
}

impl CommandResponseStream {
    fn new(id: TxLabel, inner: Arc<PeerInner>) -> CommandResponseStream {
        CommandResponseStream { id: Some(id), inner, done: false }
    }

    pub fn complete(&mut self) {
        if let Some(id) = &self.id {
            self.inner.remove_response_interest(id);
            self.id = None;
            self.done = true;
            self.inner.wake_any();
        }
    }
}

impl Unpin for CommandResponseStream {}

impl FusedStream for CommandResponseStream {
    fn is_terminated(&self) -> bool {
        self.done == true
    }
}

impl Stream for CommandResponseStream {
    type Item = Result<Packet>;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = &mut *self;
        if let Some(id) = &this.id {
            Poll::Ready(match ready!(this.inner.poll_recv_response(id, cx)) {
                Ok(packet) => {
                    trace!("received response packet {:?}", packet);
                    if packet.header().is_invalid_profile_id() {
                        Some(Err(Error::InvalidProfileId))
                    } else {
                        Some(Ok(packet))
                    }
                }
                Err(Error::PeerDisconnected) => {
                    this.done = true;
                    None
                }
                Err(e) => Some(Err(e)),
            })
        } else {
            this.done = true;
            return Poll::Ready(None);
        }
    }
}

impl Drop for CommandResponseStream {
    fn drop(&mut self) {
        self.complete();
    }
}
