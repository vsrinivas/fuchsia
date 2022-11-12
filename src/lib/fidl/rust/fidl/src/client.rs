// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a client for a fidl interface.

use {
    crate::{
        create_trace_provider,
        encoding::{
            decode_transaction_header, maybe_overflowing_after_encode, maybe_overflowing_decode,
            Decodable, Decoder, DynamicFlags, Encodable, Encoder, EpitaphBody, TransactionHeader,
            TransactionMessage,
        },
        handle::{AsyncChannel, HandleDisposition, MessageBufEtc},
        Error,
    },
    fuchsia_zircon_status as zx_status,
    futures::{
        future::{self, AndThen, FusedFuture, Future, FutureExt, MaybeDone, Ready, TryFutureExt},
        ready,
        stream::{FusedStream, Stream},
        task::{noop_waker, ArcWake, Context, Poll, Waker},
    },
    parking_lot::Mutex,
    slab::Slab,
    std::{collections::VecDeque, marker::Unpin, mem, pin::Pin, sync::Arc},
};

fn decode_transaction_body<D: Decodable, const OVERFLOWABLE: bool>(
    mut buf: MessageBufEtc,
) -> Result<D, Error> {
    let (bytes, handles) = buf.split_mut();
    let (header, body_bytes) = decode_transaction_header(bytes)?;
    let mut output = D::new_empty();

    if OVERFLOWABLE {
        maybe_overflowing_decode(&header, body_bytes, handles, &mut output)?;
    } else {
        Decoder::decode_into(&header, body_bytes, handles, &mut output)?;
    }

    Ok(output)
}

/// Decodes the body of a transaction.
/// Exposed for generated code.
pub fn decode_transaction_body_fut<D: Decodable, T, const OVERFLOWABLE: bool>(
    buf: MessageBufEtc,
    transform: fn(Result<D, Error>) -> Result<T, Error>,
) -> Ready<Result<T, Error>> {
    future::ready(transform(decode_transaction_body::<_, OVERFLOWABLE>(buf)))
}

/// A FIDL client which can be used to send buffers and receive responses via a channel.
#[derive(Debug, Clone)]
pub struct Client {
    inner: Arc<ClientInner>,
}

/// A future representing the decoded and transformed response to a FIDL query.
pub type DecodedQueryResponseFut<T> =
    AndThen<MessageResponse, Ready<Result<T, Error>>, fn(MessageBufEtc) -> Ready<Result<T, Error>>>;

/// A future representing the result of a FIDL query, with early error detection available if the
/// message couldn't be sent.
#[derive(Debug)]
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct QueryResponseFut<T>(pub MaybeDone<DecodedQueryResponseFut<T>>);

impl<T: Unpin> FusedFuture for QueryResponseFut<T> {
    fn is_terminated(&self) -> bool {
        match self.0 {
            MaybeDone::Gone => true,
            _ => false,
        }
    }
}

impl<T: Unpin> Future for QueryResponseFut<T> {
    type Output = Result<T, Error>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        ready!(self.0.poll_unpin(cx));
        let maybe_done = Pin::new(&mut self.0);
        Poll::Ready(maybe_done.take_output().unwrap_or(Err(Error::PollAfterCompletion)))
    }
}

impl<T> QueryResponseFut<T> {
    /// Check to see if the query has an error. If there was en error sending, this returns it and
    /// the error is returned, otherwise it returns self, which can then be awaited on:
    /// i.e. match echo_proxy.echo("something").check() {
    ///      Err(e) => error!("Couldn't send: {}", e),
    ///      Ok(fut) => fut.await
    /// }
    pub fn check(self) -> Result<Self, Error> {
        match self.0 {
            MaybeDone::Done(Err(e)) => Err(e),
            x => Ok(QueryResponseFut(x)),
        }
    }
}

/// A FIDL transaction id. Will not be zero for a message that includes a response.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct Txid(u32);
/// A message interest id.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
struct InterestId(usize);

impl InterestId {
    fn from_txid(txid: Txid) -> Self {
        InterestId(txid.0 as usize - 1)
    }

    fn as_raw_id(&self) -> usize {
        self.0
    }
}

impl Txid {
    fn from_interest_id(int_id: InterestId) -> Self {
        Txid((int_id.0 + 1) as u32)
    }

    /// Get the raw u32 transaction ID.
    pub fn as_raw_id(&self) -> u32 {
        self.0
    }
}

impl From<u32> for Txid {
    fn from(txid: u32) -> Self {
        Self(txid)
    }
}

impl Client {
    /// Create a new client.
    ///
    /// `channel` is the asynchronous channel over which data is sent and received.
    /// `event_ordinals` are the ordinals on which events will be received.
    pub fn new(channel: AsyncChannel, protocol_name: &'static str) -> Client {
        // Initialize tracing. This is a no-op if FIDL userspace tracing is
        // disabled or if the function was already called.
        create_trace_provider();
        Client {
            inner: Arc::new(ClientInner {
                channel,
                message_interests: Mutex::new(Slab::<MessageInterest>::new()),
                event_channel: Mutex::default(),
                epitaph: Mutex::default(),
                protocol_name,
            }),
        }
    }

    /// Get a reference to the client's underlying channel.
    pub fn as_channel(&self) -> &AsyncChannel {
        &self.inner.channel
    }

    /// Attempt to convert the `Client` back into a channel.
    ///
    /// This will only succeed if there are no active clones of this `Client`,
    /// no currently-alive `EventReceiver` or `MessageResponse`s that came from
    /// this `Client`, and no outstanding messages awaiting a response, even if
    /// that response will be discarded.
    pub fn into_channel(self) -> Result<AsyncChannel, Self> {
        // We need to check the message_interests table to make sure there are
        // no outstanding interests, since an interest might still exist even if
        // all EventReceivers and MessageResponses have been dropped. That would
        // lead to returning an AsyncChannel which could then later receive the
        // outstanding response unexpectedly.
        //
        // We do try_unwrap before checking the message_interests to avoid a
        // race where another thread inserts a new value into message_interests
        // after we check message_interests.is_empty(), but before we get to
        // try_unwrap. This forces us to create a new Arc if message_interests
        // isn't empty, since try_unwrap destroys the original Arc.
        match Arc::try_unwrap(self.inner) {
            Ok(inner) => {
                if inner.message_interests.lock().is_empty() || inner.channel.is_closed() {
                    Ok(inner.channel)
                } else {
                    // This creates a new arc if there are outstanding
                    // interests. This is ok because we never create any weak
                    // references to ClientInner, otherwise doing this would
                    // detach weak references.
                    Err(Self { inner: Arc::new(inner) })
                }
            }
            Err(inner) => Err(Self { inner }),
        }
    }

    /// Retrieve the stream of event messages for the `Client`.
    /// Panics if the stream was already taken.
    pub fn take_event_receiver(&self) -> EventReceiver {
        {
            let mut lock = self.inner.event_channel.lock();

            if let EventListener::None = lock.listener {
                lock.listener = EventListener::WillPoll;
            } else {
                panic!("Event stream was already taken");
            }
        }

        EventReceiver { inner: self.inner.clone(), state: EventReceiverState::Active }
    }

    /// Send an encodable message without expecting a response.
    pub fn send<T: Encodable, const OVERFLOWABLE: bool>(
        &self,
        body: &mut T,
        ordinal: u64,
        dynamic_flags: DynamicFlags,
    ) -> Result<(), Error> {
        let msg = &mut TransactionMessage {
            header: TransactionHeader::new(0, ordinal, dynamic_flags),
            body,
        };
        crate::encoding::with_tls_encoded::<_, _, OVERFLOWABLE>(msg, |bytes, handles| {
            self.send_raw_msg(&**bytes, handles)
        })
    }

    /// Send an encodable query and receive a decodable response.
    pub fn send_query<
        E: Encodable,
        D: Decodable,
        const REQUEST_ENCODE_OVERFLOWABLE: bool,
        const RESPONSE_DECODE_OVERFLOWABLE: bool,
    >(
        &self,
        msg: &mut E,
        ordinal: u64,
        dynamic_flags: DynamicFlags,
    ) -> QueryResponseFut<D> {
        let send_result = self.send_raw_query(|tx_id, bytes, handles| {
            let msg = &mut TransactionMessage {
                header: TransactionHeader::new(tx_id.as_raw_id(), ordinal, dynamic_flags),
                body: msg,
            };
            Encoder::encode(bytes, handles, msg)?;

            if REQUEST_ENCODE_OVERFLOWABLE {
                maybe_overflowing_after_encode(bytes, handles)?;
            }

            Ok(())
        });

        QueryResponseFut(match send_result {
            Ok(res_fut) => future::maybe_done(res_fut.and_then(|buf| {
                decode_transaction_body_fut::<D, D, RESPONSE_DECODE_OVERFLOWABLE>(
                    buf,
                    std::convert::identity,
                )
            })),
            Err(e) => MaybeDone::Done(Err(e)),
        })
    }

    /// Send an encodable query and receive a decodable response, transforming
    /// the response with the specified function after decoding.
    pub fn call_send_raw_query<E: Encodable, const OVERFLOWABLE: bool>(
        &self,
        msg: &mut E,
        ordinal: u64,
        dynamic_flags: DynamicFlags,
    ) -> Result<MessageResponse, Error> {
        self.send_raw_query(|tx_id, bytes, handles| {
            let msg = &mut TransactionMessage {
                header: TransactionHeader::new(tx_id.as_raw_id(), ordinal, dynamic_flags),
                body: msg,
            };
            Encoder::encode(bytes, handles, msg)?;

            if OVERFLOWABLE {
                maybe_overflowing_after_encode(bytes, handles)?;
            }

            Ok(())
        })
    }

    /// Send a raw message without expecting a response.
    pub fn send_raw_msg(
        &self,
        bytes: &[u8],
        handles: &mut Vec<HandleDisposition<'_>>,
    ) -> Result<(), Error> {
        match self.inner.channel.write_etc(bytes, handles) {
            Ok(()) => Ok(()),
            Err(zx_status::Status::PEER_CLOSED) => {
                Err(Error::ClientChannelClosed {
                    // Try to receive the epitaph.
                    status: self.inner.recv_all()?.unwrap_or(zx_status::Status::PEER_CLOSED),
                    protocol_name: self.inner.protocol_name,
                })
            }
            Err(e) => Err(Error::ClientWrite(e.into())),
        }
    }

    /// Send a raw query and receive a response future.
    pub fn send_raw_query<F>(&self, msg_from_id: F) -> Result<MessageResponse, Error>
    where
        F: for<'a, 'b> FnOnce(
            Txid,
            &'a mut Vec<u8>,
            &'b mut Vec<HandleDisposition<'_>>,
        ) -> Result<(), Error>,
    {
        let id = self.inner.register_msg_interest();
        crate::encoding::with_tls_encode_buf(|bytes, handles| {
            msg_from_id(Txid::from_interest_id(id), bytes, handles)?;
            self.send_raw_msg(bytes, handles)
        })?;

        Ok(MessageResponse { id: Txid::from_interest_id(id), client: Some(self.inner.clone()) })
    }
}

#[must_use]
/// A future which polls for the response to a client message.
#[derive(Debug)]
pub struct MessageResponse {
    id: Txid,
    // `None` if the message response has been received
    client: Option<Arc<ClientInner>>,
}

impl Unpin for MessageResponse {}

impl Future for MessageResponse {
    type Output = Result<MessageBufEtc, Error>;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        let res;
        {
            let client = this.client.as_ref().ok_or(Error::PollAfterCompletion)?;
            res = client.poll_recv_msg_response(this.id, cx);
        }

        // Drop the client reference if the response has been received
        if let Poll::Ready(Ok(_)) = res {
            this.client.take().expect("MessageResponse polled after completion");
        }

        res
    }
}

impl Drop for MessageResponse {
    fn drop(&mut self) {
        if let Some(client) = &self.client {
            client.deregister_msg_interest(InterestId::from_txid(self.id));
        }
    }
}

/// An enum reprenting either a resolved message interest or a task on which to alert
/// that a response message has arrived.
#[derive(Debug)]
enum MessageInterest {
    /// A new `MessageInterest`
    WillPoll,
    /// A task is waiting to receive a response, and can be awoken with `Waker`.
    Waiting(Waker),
    /// A message has been received, and a task will poll to receive it.
    Received(MessageBufEtc),
    /// A message has not been received, but the person interested in the response
    /// no longer cares about it, so the message should be discared upon arrival.
    Discard,
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

    fn unwrap_received(self) -> MessageBufEtc {
        if let MessageInterest::Received(buf) = self {
            buf
        } else {
            panic!("EXPECTED received message")
        }
    }

    /// Registers the waker from `cx` if the message has not already been received, replacing any
    /// previous waker registered.
    fn register(&mut self, cx: &mut Context<'_>) {
        if self.is_received() {
            return;
        }
        if let Self::Discard = self {
            panic!("Polled a discarded MessageReceiver?!");
        }
        // Must be either WillPoll or Waiting, replace the waker.
        *self = Self::Waiting(cx.waker().clone());
    }

    /// Receive a message for this MessageInterest, waking the waiter if they are waiting to
    /// poll.
    /// Returns true if the task interested in the response no longer cares about it, in which
    /// case this can be cleaned up.
    fn receive(&mut self, message: MessageBufEtc) -> bool {
        if let Self::Discard = self {
            return true;
        } else if let Self::Waiting(waker) = mem::replace(self, Self::Received(message)) {
            waker.wake();
        }
        false
    }

    /// Wake the interested task, if it is waiting, putting it in a WillPoll state.
    /// This function is idempotent.
    fn wake(&mut self) {
        if let Self::Waiting(waker) = self {
            waker.wake_by_ref();
            *self = Self::WillPoll;
        }
    }
}

#[derive(Debug)]
enum EventReceiverState {
    Active,
    Epitaph,
    Terminated,
}

/// A stream of events as `MessageBufEtc`s.
#[derive(Debug)]
pub struct EventReceiver {
    inner: Arc<ClientInner>,
    state: EventReceiverState,
}

impl Unpin for EventReceiver {}

impl FusedStream for EventReceiver {
    fn is_terminated(&self) -> bool {
        match self.state {
            EventReceiverState::Terminated => true,
            _ => false,
        }
    }
}

/// This implementation holds up two invariants
///   (1) After `None` is returned, the next poll panics
///   (2) Until this instance is dropped, no other EventReceiver may claim the
///       event channel by calling Client::take_event_receiver.
impl Stream for EventReceiver {
    type Item = Result<MessageBufEtc, Error>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        match self.state {
            EventReceiverState::Active => {}
            EventReceiverState::Terminated => {
                panic!("polled EventReceiver after `None`");
            }
            EventReceiverState::Epitaph => {
                self.state = EventReceiverState::Terminated;
                return Poll::Ready(None);
            }
        }

        Poll::Ready(match ready!(self.inner.as_ref().poll_recv_event(cx)) {
            Ok(x) => Some(Ok(x)),
            Err(Error::ClientChannelClosed { status: zx_status::Status::PEER_CLOSED, .. }) => {
                // The channel is closed, with no epitaph. Set our internal state so that on
                // the next poll_next() we panic and is_terminated() returns an appropriate value.
                self.state = EventReceiverState::Terminated;
                None
            }
            err @ Err(Error::ClientChannelClosed { .. }) => {
                // The channel is closed with an epitaph. Return the epitaph and set our internal
                // state so that on the next poll_next() we return a None and terminate the stream.
                self.state = EventReceiverState::Epitaph;
                Some(err)
            }
            Err(e) => Some(Err(e)),
        })
    }
}

impl Drop for EventReceiver {
    fn drop(&mut self) {
        self.inner.event_channel.lock().listener = EventListener::None;
    }
}

#[derive(Debug, Default)]
struct EventChannel {
    listener: EventListener,
    queue: VecDeque<MessageBufEtc>,
}

#[derive(Debug)]
enum EventListener {
    /// No one is listening for the event
    None,
    /// Someone is listening for the event but has not yet polled
    WillPoll,
    /// Someone is listening for the event and can be woken via the `Waker`
    Some(Waker),
}

impl Default for EventListener {
    fn default() -> Self {
        EventListener::None
    }
}

impl EventListener {
    /// Wakes this, putting it in a WillPoll State until it is polled again.
    fn wake(&mut self) {
        *self = match mem::replace(self, Self::None) {
            Self::Some(waker) => {
                waker.wake();
                Self::WillPoll
            }
            x => x,
        };
    }
}

struct CombinedWaker {
    wakers: Vec<Waker>,
}

impl CombinedWaker {
    fn make_waker(wakers: Vec<Waker>) -> Waker {
        futures::task::waker(Arc::new(Self { wakers }))
    }
}

impl ArcWake for CombinedWaker {
    fn wake_by_ref(arc_self: &Arc<Self>) {
        for waker in &arc_self.wakers {
            waker.wake_by_ref();
        }
    }
}

/// A shared client channel which tracks EXPECTED and received responses
#[derive(Debug)]
struct ClientInner {
    /// The channel that leads to the server we are connected to.
    channel: AsyncChannel,

    /// A map of MessageInterests, which track state of responses to two-way
    /// messages.
    /// An interest is registered here with `register_msg_interest` and deregistered
    /// by either retrieving a message via a call to `poll_recv_msg_response` or manually
    /// deregistering with `deregister_msg_interest`
    message_interests: Mutex<Slab<MessageInterest>>,

    /// A queue of received events and a waker for the task to receive them.
    event_channel: Mutex<EventChannel>,

    /// The server provided epitaph, or None if the channel is not closed.
    epitaph: Mutex<Option<zx_status::Status>>,

    /// The `ProtocolMarker::DEBUG_NAME` for the service this client connects to.
    protocol_name: &'static str,
}

impl ClientInner {
    /// Registers interest in a response message.
    ///
    /// This function returns a `usize` ID which should be used to send a message
    /// via the channel. Responses are then received using `poll_recv`.
    fn register_msg_interest(&self) -> InterestId {
        // TODO(cramertj) use `try_from` here and assert that the conversion from
        // `usize` to `u32` hasn't overflowed.
        InterestId(self.message_interests.lock().insert(MessageInterest::WillPoll))
    }

    fn poll_recv_event(&self, cx: &mut Context<'_>) -> Poll<Result<MessageBufEtc, Error>> {
        {
            // Update the EventListener with the latest waker, remove any stale WillPoll state
            let mut lock = self.event_channel.lock();
            lock.listener = EventListener::Some(cx.waker().clone());
        }

        // Process any data on the channel, registering any tasks still waiting to wake when the
        // channel becomes ready.
        let epitaph = self.recv_all()?;

        let mut lock = self.event_channel.lock();

        if let Some(msg_buf) = lock.queue.pop_front() {
            Poll::Ready(Ok(msg_buf))
        } else {
            if let Some(status) = epitaph {
                Poll::Ready(Err(Error::ClientChannelClosed {
                    status,
                    protocol_name: self.protocol_name,
                }))
            } else {
                Poll::Pending
            }
        }
    }

    /// Poll for the response to `txid`, registering the waker associated with `cx` to be awoken,
    /// or returning the response buffer if it has been received.
    fn poll_recv_msg_response(
        &self,
        txid: Txid,
        cx: &mut Context<'_>,
    ) -> Poll<Result<MessageBufEtc, Error>> {
        let interest_id = InterestId::from_txid(txid);
        {
            // Register our waker with the interest if we haven't received a message yet.
            let mut message_interests = self.message_interests.lock();
            message_interests
                .get_mut(interest_id.as_raw_id())
                .expect("Polled unregistered interest")
                .register(cx);
        }

        // Process any data on the channel, registering tasks still waiting for wake when the
        // channel becomes ready.
        let epitaph = self.recv_all()?;

        let mut message_interests = self.message_interests.lock();
        if message_interests
            .get(interest_id.as_raw_id())
            .expect("Polled unregistered interest")
            .is_received()
        {
            // If we got the result remove the received buffer and return, freeing up the
            // space for a new message.
            let buf = message_interests.remove(interest_id.as_raw_id()).unwrap_received();
            Poll::Ready(Ok(buf))
        } else {
            if let Some(status) = epitaph {
                Poll::Ready(Err(Error::ClientChannelClosed {
                    status,
                    protocol_name: self.protocol_name,
                }))
            } else {
                Poll::Pending
            }
        }
    }

    /// Poll for the receipt of any response message or an event.
    /// Wakers present in any MessageInterest or the EventReceiver when this is called will be
    /// notified when their message arrives or when there is new data if the channel is empty.
    ///
    /// Returns the epitaph (or PEER_CLOSED) if the channel was closed, and None otherwise.
    fn recv_all(&self) -> Result<Option<zx_status::Status>, Error> {
        // TODO(cramertj) return errors if one has occurred _ever_ in recv_all, not just if
        // one happens on this call.
        loop {
            // Acquire a mutex so that only one thread can read from the underlying channel
            // at a time. Channel is already synchronized, but we need to also decode the
            // FIDL message header atomically so that epitaphs can be properly handled.
            let mut epitaph_lock = self.epitaph.lock();
            if epitaph_lock.is_some() {
                return Ok(*epitaph_lock);
            }
            let buf = {
                // Get a combined waker that will wake up everyone who is waiting.
                let waker = self.get_combined_waker();
                let cx = &mut Context::from_waker(&waker);

                let mut buf = MessageBufEtc::new();
                let result = self.channel.recv_etc_from(cx, &mut buf);
                match result {
                    Poll::Ready(Ok(())) => {}
                    Poll::Ready(Err(zx_status::Status::PEER_CLOSED)) => {
                        // The channel has been closed, and no epitaph was received.
                        // Set the epitaph to PEER_CLOSED.
                        *epitaph_lock = Some(zx_status::Status::PEER_CLOSED);
                        // Wake up everyone waiting, since an epitaph is broadcast to all receivers.
                        self.wake_all();
                        return Ok(*epitaph_lock);
                    }
                    Poll::Ready(Err(e)) => return Err(Error::ClientRead(e)),
                    Poll::Pending => {
                        return Ok(None);
                    }
                };
                buf
            };

            let (header, body_bytes) =
                decode_transaction_header(buf.bytes()).map_err(|_| Error::InvalidHeader)?;
            if !header.is_compatible() {
                return Err(Error::IncompatibleMagicNumber(header.magic_number()));
            }
            if header.is_epitaph() {
                // Received an epitaph. Record this so that everyone receives the same epitaph.
                let handles = &mut [];
                let mut epitaph_body = EpitaphBody::new_empty();
                Decoder::decode_into(&header, &body_bytes, handles, &mut epitaph_body)?;
                *epitaph_lock = Some(epitaph_body.error);
                // Wake up everyone waiting, since an epitaph is broadcast to all receivers.
                self.wake_all();
                return Ok(*epitaph_lock);
            }

            // Epitaph handling is done, so the lock is no longer required.
            drop(epitaph_lock);

            if header.tx_id() == 0 {
                // received an event
                let mut lock = self.event_channel.lock();
                lock.queue.push_back(buf);
                lock.listener.wake();
            } else {
                // received a message response
                let recvd_interest_id = InterestId::from_txid(Txid(header.tx_id()));

                // Look for a message interest with the given ID.
                // If one is found, store the message so that it can be picked up later.
                let mut message_interests = self.message_interests.lock();
                let raw_recvd_interest_id = recvd_interest_id.as_raw_id();
                // TODO(fxbug.dev/114743): Unknown transaction IDs should cause
                // an error/close the channel.
                if let Some(interest) = message_interests.get_mut(raw_recvd_interest_id) {
                    let remove = interest.receive(buf);
                    if remove {
                        message_interests.remove(raw_recvd_interest_id);
                    }
                }
            }
        }
    }

    fn deregister_msg_interest(&self, InterestId(id): InterestId) {
        let mut lock = self.message_interests.lock();
        if lock[id].is_received() {
            lock.remove(id);
        } else {
            lock[id] = MessageInterest::Discard;
        }
    }

    /// Gets a waker that will wake up all the tasks that are waiting on this channel.
    /// `wake_all` is preferred if you are certain you are waking everyone immediately, as it is
    /// idempotent (it will only wake each task once)
    // TODO(fxbug.dev/74427): if Arc::new_cyclic becomes stable, we can wake tasks only when their
    // message has arrived.
    fn get_combined_waker(&self) -> Waker {
        let mut wakers = Vec::new();
        {
            let lock = self.message_interests.lock();
            wakers.reserve(lock.len() + 1);
            for (_, message_interest) in lock.iter() {
                if let MessageInterest::Waiting(waker) = message_interest {
                    wakers.push(waker.clone());
                }
            }
        }
        {
            let lock = self.event_channel.lock();
            if let EventListener::Some(waker) = &lock.listener {
                wakers.push(waker.clone());
            }
        }
        if !wakers.is_empty() {
            wakers.shrink_to_fit();
            CombinedWaker::make_waker(wakers)
        } else {
            noop_waker()
        }
    }

    /// Wakes all tasks that have polled on this channel.
    fn wake_all(&self) {
        {
            let mut lock = self.message_interests.lock();
            for (_, interest) in lock.iter_mut() {
                interest.wake();
            }
        }
        self.event_channel.lock().listener.wake();
    }
}

#[cfg(target_os = "fuchsia")]
pub mod sync {
    //! Synchronous FIDL Client

    use {
        crate::encoding::{maybe_overflowing_after_encode, maybe_overflowing_decode},
        fuchsia_zircon::{self as zx, AsHandleRef},
    };

    use super::*;

    /// A synchronous client for making FIDL calls.
    #[derive(Debug)]
    pub struct Client {
        // Underlying channel
        channel: zx::Channel,

        // The `ProtocolMarker::DEBUG_NAME` for the service this client connects to.
        protocol_name: &'static str,
    }

    impl Client {
        /// Create a new synchronous FIDL client.
        pub fn new(channel: zx::Channel, protocol_name: &'static str) -> Self {
            // Initialize tracing. This is a no-op if FIDL userspace tracing is
            // disabled or if the function was already called.
            create_trace_provider();
            Client { channel, protocol_name }
        }

        /// Get the underlying channel out of the client.
        pub fn into_channel(self) -> zx::Channel {
            self.channel
        }

        /// Send a new message.
        pub fn send<E: Encodable, const OVERFLOWABLE: bool>(
            &self,
            msg: &mut E,
            ordinal: u64,
            dynamic_flags: DynamicFlags,
        ) -> Result<(), Error> {
            let mut write_bytes = Vec::new();
            let mut write_handles = Vec::new();

            let msg = &mut TransactionMessage {
                header: TransactionHeader::new(0, ordinal, dynamic_flags),
                body: msg,
            };
            Encoder::encode(&mut write_bytes, &mut write_handles, msg)?;

            if OVERFLOWABLE {
                maybe_overflowing_after_encode(&mut write_bytes, &mut write_handles)?;
            }

            self.channel
                .write_etc(&mut write_bytes, &mut write_handles)
                .map_err(|e| self.wrap_error(Error::ClientWrite, e))?;
            Ok(())
        }

        /// Send a new message expecting a response.
        pub fn send_query<
            E: Encodable,
            D: Decodable,
            const REQUEST_ENCODE_OVERFLOWABLE: bool,
            const RESPONSE_DECODE_OVERFLOWABLE: bool,
        >(
            &self,
            msg: &mut E,
            ordinal: u64,
            dynamic_flags: DynamicFlags,
            deadline: zx::Time,
        ) -> Result<D, Error> {
            let mut write_bytes = Vec::new();
            let mut write_handles = Vec::new();

            let msg = &mut TransactionMessage {
                header: TransactionHeader::new(0, ordinal, dynamic_flags),
                body: msg,
            };
            Encoder::encode(&mut write_bytes, &mut write_handles, msg)?;

            if REQUEST_ENCODE_OVERFLOWABLE {
                maybe_overflowing_after_encode(&mut write_bytes, &mut write_handles)?;
            }

            let mut buf = zx::MessageBufEtc::new();
            buf.ensure_capacity_bytes(zx::sys::ZX_CHANNEL_MAX_MSG_BYTES as usize);
            buf.ensure_capacity_handle_infos(zx::sys::ZX_CHANNEL_MAX_MSG_HANDLES as usize);

            // TODO: We should be able to use the same memory to back the bytes we use for writing
            // and reading.
            self.channel
                .call_etc(deadline, &write_bytes, &mut write_handles, &mut buf)
                .map_err(|e| self.wrap_error(Error::ClientCall, e))?;

            let (bytes, mut handle_infos) = buf.split();
            let (header, body_bytes) = decode_transaction_header(&bytes)?;
            let mut output = D::new_empty();

            if RESPONSE_DECODE_OVERFLOWABLE {
                maybe_overflowing_decode(&header, body_bytes, &mut handle_infos, &mut output)?;
            } else {
                Decoder::decode_into(&header, body_bytes, &mut handle_infos, &mut output)?;
            }

            Ok(output)
        }

        /// Wait for an event to arrive on the underlying channel.
        pub fn wait_for_event(&self, deadline: zx::Time) -> Result<MessageBufEtc, Error> {
            let mut buf = zx::MessageBufEtc::new();
            buf.ensure_capacity_bytes(zx::sys::ZX_CHANNEL_MAX_MSG_BYTES as usize);
            buf.ensure_capacity_handle_infos(zx::sys::ZX_CHANNEL_MAX_MSG_HANDLES as usize);

            loop {
                self.channel
                    .wait_handle(
                        zx::Signals::CHANNEL_READABLE | zx::Signals::CHANNEL_PEER_CLOSED,
                        deadline,
                    )
                    .map_err(|e| self.wrap_error(Error::ClientEvent, e))?;
                match self.channel.read_etc(&mut buf) {
                    Ok(()) => {
                        // We succeeded in reading the message. Check that it is
                        // an event not a two-way method reply.
                        let (header, _) = decode_transaction_header(buf.bytes())
                            .map_err(|_| Error::InvalidHeader)?;
                        if header.tx_id() != 0 {
                            return Err(Error::UnexpectedSyncResponse);
                        }
                        return Ok(buf);
                    }
                    Err(zx::Status::SHOULD_WAIT) => {
                        // Some other thread read the message we woke up to read.
                        continue;
                    }
                    Err(e) => {
                        return Err(self.wrap_error(Error::ClientRead, e));
                    }
                }
            }
        }

        /// Wraps an error in the given `variant` of the `Error` enum, except
        /// for `zx_status::Status::PEER_CLOSED`, in which case it uses the
        /// `Error::ClientChannelClosed` variant.
        fn wrap_error<T: Fn(zx_status::Status) -> Error>(
            &self,
            variant: T,
            err: zx_status::Status,
        ) -> Error {
            if err == zx_status::Status::PEER_CLOSED {
                Error::ClientChannelClosed {
                    status: zx_status::Status::PEER_CLOSED,
                    protocol_name: self.protocol_name,
                }
            } else {
                variant(err)
            }
        }
    }
}

#[cfg(all(test, target_os = "fuchsia"))]
mod tests {
    use super::*;
    use {
        crate::encoding::MAGIC_NUMBER_INITIAL,
        crate::epitaph::{self, ChannelEpitaphExt},
        anyhow::{Context as _, Error},
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        fuchsia_async::{DurationExt, TimeoutExt},
        fuchsia_zircon as zx,
        fuchsia_zircon::{AsHandleRef, DurationNum},
        futures::{join, FutureExt, StreamExt},
        futures_test::task::new_count_waker,
        std::thread,
    };

    const SEND_ORDINAL_HIGH_BYTE: u8 = 42;
    const SEND_ORDINAL: u64 = 42 << 32;
    const SEND_DATA: u8 = 55;

    const EVENT_ORDINAL: u64 = 854 << 23;

    #[rustfmt::skip]
    fn expected_sent_bytes(tx_id_low_byte: u8) -> [u8; 24] {
        [
            tx_id_low_byte, 0, 0, 0, // 32 bit tx_id
            2, 0, 0, // flags
            MAGIC_NUMBER_INITIAL,
            0, 0, 0, 0, // low bytes of 64 bit ordinal
            SEND_ORDINAL_HIGH_BYTE, 0, 0, 0, // high bytes of 64 bit ordinal
            SEND_DATA, // 8 bit data
            0, 0, 0, 0, 0, 0, 0, // 7 bytes of padding after our 1 byte of data
        ]
    }

    fn send_transaction(header: TransactionHeader, channel: &zx::Channel) {
        let (bytes, handles) = (&mut vec![], &mut vec![]);
        encode_transaction(header, bytes, handles);
        channel.write_etc(bytes, handles).expect("Server channel write failed");
    }

    fn encode_transaction<'a>(
        header: TransactionHeader,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<zx::HandleDisposition<'a>>,
    ) {
        let event = &mut TransactionMessage { header: header, body: &mut SEND_DATA.clone() };
        Encoder::encode(bytes, handles, event).expect("Encoding failure");
    }

    #[test]
    fn sync_client() -> Result<(), Error> {
        let (client_end, server_end) = zx::Channel::create().context("chan create")?;
        let client = sync::Client::new(client_end, "test_protocol");
        client
            .send::<u8, false>(&mut SEND_DATA.clone(), SEND_ORDINAL, DynamicFlags::empty())
            .context("sending")?;
        let mut received = MessageBufEtc::new();
        server_end.read_etc(&mut received).context("reading")?;
        let one_way_tx_id = 0;
        assert_eq!(received.bytes(), expected_sent_bytes(one_way_tx_id));
        Ok(())
    }

    #[test]
    fn sync_client_with_response() -> Result<(), Error> {
        let (client_end, server_end) = zx::Channel::create().context("chan create")?;
        let client = sync::Client::new(client_end, "test_protocol");
        thread::spawn(move || {
            // Server
            let mut received = MessageBufEtc::new();
            server_end
                .wait_handle(zx::Signals::CHANNEL_READABLE, zx::Time::after(5.seconds()))
                .expect("failed to wait for channel readable");
            server_end.read_etc(&mut received).expect("failed to read on server end");
            let (buf, _handles) = received.split_mut();
            let (header, _body_bytes) = decode_transaction_header(buf).expect("server decode");
            assert_eq!(header.ordinal(), SEND_ORDINAL);
            send_transaction(
                TransactionHeader::new(header.tx_id(), header.ordinal(), DynamicFlags::empty()),
                &server_end,
            );
        });
        let response_data = client
            .send_query::<u8, u8, false, false>(
                &mut SEND_DATA.clone(),
                SEND_ORDINAL,
                DynamicFlags::empty(),
                zx::Time::after(5.seconds()),
            )
            .context("sending query")?;
        assert_eq!(SEND_DATA, response_data);
        Ok(())
    }

    #[test]
    fn sync_client_with_event_and_response() -> Result<(), Error> {
        let (client_end, server_end) = zx::Channel::create().context("chan create")?;
        let client = sync::Client::new(client_end, "test_protocol");
        thread::spawn(move || {
            // Server
            let mut received = MessageBufEtc::new();
            server_end
                .wait_handle(zx::Signals::CHANNEL_READABLE, zx::Time::after(5.seconds()))
                .expect("failed to wait for channel readable");
            server_end.read_etc(&mut received).expect("failed to read on server end");
            let (buf, _handles) = received.split_mut();
            let (header, _body_bytes) = decode_transaction_header(buf).expect("server decode");
            assert_ne!(header.tx_id(), 0);
            assert_eq!(header.ordinal(), SEND_ORDINAL);
            // First, send an event.
            send_transaction(
                TransactionHeader::new(0, EVENT_ORDINAL, DynamicFlags::empty()),
                &server_end,
            );
            // Then send the reply. The kernel should pick the correct message to deliver based
            // on the tx_id.
            send_transaction(
                TransactionHeader::new(header.tx_id(), header.ordinal(), DynamicFlags::empty()),
                &server_end,
            );
        });
        let response_data = client
            .send_query::<u8, u8, false, false>(
                &mut SEND_DATA.clone(),
                SEND_ORDINAL,
                DynamicFlags::empty(),
                zx::Time::after(5.seconds()),
            )
            .context("sending query")?;
        assert_eq!(SEND_DATA, response_data);

        let event_buf =
            client.wait_for_event(zx::Time::after(5.seconds())).context("waiting for event")?;
        let (bytes, _handles) = event_buf.split();
        let (header, _body) = decode_transaction_header(&bytes).expect("event decode");
        assert_eq!(header.ordinal(), EVENT_ORDINAL);

        Ok(())
    }

    #[test]
    fn sync_client_with_racing_events() -> Result<(), Error> {
        let (client_end, server_end) = zx::Channel::create().context("chan create")?;
        let client1 = Arc::new(sync::Client::new(client_end, "test_protocol"));
        let client2 = client1.clone();

        let thread1 = thread::spawn(move || {
            let result = client1.wait_for_event(zx::Time::after(5.seconds()));
            assert!(result.is_ok());
        });

        let thread2 = thread::spawn(move || {
            let result = client2.wait_for_event(zx::Time::after(5.seconds()));
            assert!(result.is_ok());
        });

        send_transaction(
            TransactionHeader::new(0, EVENT_ORDINAL, DynamicFlags::empty()),
            &server_end,
        );
        send_transaction(
            TransactionHeader::new(0, EVENT_ORDINAL, DynamicFlags::empty()),
            &server_end,
        );

        assert!(thread1.join().is_ok());
        assert!(thread2.join().is_ok());

        Ok(())
    }

    #[test]
    fn sync_client_wait_for_event_gets_method_response() -> Result<(), Error> {
        let (client_end, server_end) = zx::Channel::create().context("chan create")?;
        let client = sync::Client::new(client_end, "test_protocol");
        send_transaction(
            TransactionHeader::new(3902304923, SEND_ORDINAL, DynamicFlags::empty()),
            &server_end,
        );
        assert_matches!(
            client.wait_for_event(zx::Time::after(5.seconds())),
            Err(crate::Error::UnexpectedSyncResponse)
        );
        Ok(())
    }

    #[test]
    fn sync_client_peer_closed() -> Result<(), Error> {
        let (client_end, server_end) = zx::Channel::create().context("chan create")?;
        let client = sync::Client::new(client_end, "test_protocol");
        // Close the server channel.
        drop(server_end);
        assert_matches!(
            client.send::<u8, false>(&mut SEND_DATA.clone(), SEND_ORDINAL, DynamicFlags::empty()),
            Err(crate::Error::ClientChannelClosed {
                status: zx_status::Status::PEER_CLOSED,
                protocol_name: "test_protocol",
            })
        );
        Ok(())
    }

    // TODO(fxbug.dev/73477): When the sync client supports epitaphs, rename
    // this and change the assert to expect zx_status::Status::UNAVAILABLE.
    #[test]
    fn sync_client_does_not_receive_epitaphs() -> Result<(), Error> {
        let (client_end, server_end) = zx::Channel::create().context("chan create")?;
        let client = sync::Client::new(client_end, "test_protocol");
        // Close the server channel with an epitaph.
        server_end
            .close_with_epitaph(zx_status::Status::UNAVAILABLE)
            .expect("failed to write epitaph");
        assert_matches!(
            client.send::<u8, false>(&mut SEND_DATA.clone(), SEND_ORDINAL, DynamicFlags::empty()),
            Err(crate::Error::ClientChannelClosed {
                status: zx_status::Status::PEER_CLOSED,
                protocol_name: "test_protocol",
            })
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn client() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        let server = AsyncChannel::from_channel(server_end).unwrap();
        let receiver = async move {
            let mut buffer = MessageBufEtc::new();
            server.recv_etc_msg(&mut buffer).await.expect("failed to recv msg");
            let one_way_tx_id = 0;
            assert_eq!(buffer.bytes(), expected_sent_bytes(one_way_tx_id));
        };

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver
            .on_timeout(300.millis().after_now(), || panic!("did not receive message in time!"));

        client
            .send::<u8, false>(&mut SEND_DATA.clone(), SEND_ORDINAL, DynamicFlags::empty())
            .expect("failed to send msg");

        receiver.await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_with_response() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        let server = AsyncChannel::from_channel(server_end).unwrap();
        let mut buffer = MessageBufEtc::new();
        let receiver = async move {
            server.recv_etc_msg(&mut buffer).await.expect("failed to recv msg");
            let two_way_tx_id = 1u8;
            assert_eq!(buffer.bytes(), expected_sent_bytes(two_way_tx_id));

            let (bytes, handles) = (&mut vec![], &mut vec![]);
            let header = TransactionHeader::new(two_way_tx_id as u32, 42, DynamicFlags::empty());
            encode_transaction(header, bytes, handles);
            server.write_etc(bytes, handles).expect("Server channel write failed");
        };

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver
            .on_timeout(300.millis().after_now(), || panic!("did not receiver message in time!"));

        let sender = client
            .send_query::<u8, u8, false, false>(
                &mut SEND_DATA.clone(),
                SEND_ORDINAL,
                DynamicFlags::empty(),
            )
            .map_ok(|x| assert_eq!(x, SEND_DATA))
            .unwrap_or_else(|e| panic!("fidl error: {:?}", e));

        // add a timeout to receiver so if test is broken it doesn't take forever
        let sender = sender
            .on_timeout(300.millis().after_now(), || panic!("did not receive response in time!"));

        let ((), ()) = join!(receiver, sender);
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_with_response_receives_epitaph() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        let server = AsyncChannel::from_channel(server_end).unwrap();
        let mut buffer = zx::MessageBufEtc::new();
        let receiver = async move {
            server.recv_etc_msg(&mut buffer).await.expect("failed to recv msg");
            server
                .close_with_epitaph(zx_status::Status::UNAVAILABLE)
                .expect("failed to write epitaph");
        };
        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver
            .on_timeout(300.millis().after_now(), || panic!("did not receive message in time!"));

        let sender = async move {
            let result = client
                .send_query::<u8, u8, false, false>(&mut 55, 42 << 32, DynamicFlags::empty())
                .await;
            assert_matches!(
                result,
                Err(crate::Error::ClientChannelClosed {
                    status: zx_status::Status::UNAVAILABLE,
                    protocol_name: "test_protocol"
                })
            );
        };
        // add a timeout to sender so if test is broken it doesn't take forever
        let sender = sender
            .on_timeout(300.millis().after_now(), || panic!("did not receive response in time!"));

        let ((), ()) = join!(receiver, sender);
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic]
    async fn event_cant_be_taken_twice() {
        let (client_end, _) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");
        let _foo = client.take_event_receiver();
        client.take_event_receiver();
    }

    #[fasync::run_singlethreaded(test)]
    async fn event_can_be_taken_after_drop() {
        let (client_end, _) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");
        let foo = client.take_event_receiver();
        drop(foo);
        client.take_event_receiver();
    }

    #[fasync::run_singlethreaded(test)]
    async fn receiver_termination_test() {
        let (client_end, _) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");
        let mut foo = client.take_event_receiver();
        assert!(!foo.is_terminated(), "receiver should not report terminated before being polled");
        let _ = foo.next().await;
        assert!(
            foo.is_terminated(),
            "receiver should report terminated after seeing channel is closed"
        );
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "polled EventReceiver after `None`")]
    async fn receiver_cant_be_polled_more_than_once_on_closed_stream() {
        let (client_end, _) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");
        let foo = client.take_event_receiver();
        drop(foo);
        let mut bar = client.take_event_receiver();
        assert!(bar.next().await.is_none(), "read on closed channel should return none");
        // this should panic
        let _ = bar.next().await;
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "polled EventReceiver after `None`")]
    async fn receiver_panics_when_polled_after_receiving_epitaph_then_none() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");
        let mut stream = client.take_event_receiver();

        epitaph::write_epitaph_impl(&server_end, zx_status::Status::UNAVAILABLE)
            .expect("wrote epitaph");
        drop(server_end);

        assert_matches!(
            stream.next().await,
            Some(Err(crate::Error::ClientChannelClosed {
                status: zx_status::Status::UNAVAILABLE,
                protocol_name: "test_protocol"
            }))
        );
        assert_matches!(stream.next().await, None);
        // this should panic
        let _ = stream.next().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn event_can_be_taken() {
        let (client_end, _) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");
        client.take_event_receiver();
    }

    #[fasync::run_singlethreaded(test)]
    async fn event_received() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        // Send the event from the server
        let server = AsyncChannel::from_channel(server_end).unwrap();
        let (bytes, handles) = (&mut vec![], &mut vec![]);
        let header = TransactionHeader::new(0, 5, DynamicFlags::empty());
        encode_transaction(header, bytes, handles);
        server.write_etc(bytes, handles).expect("Server channel write failed");
        drop(server);

        let recv = client
            .take_event_receiver()
            .into_future()
            .then(|(x, stream)| {
                let x = x.expect("should contain one element");
                let x = x.expect("fidl error");
                let x: i32 =
                    decode_transaction_body::<_, false>(x).expect("failed to decode event");
                assert_eq!(x, 55);
                stream.into_future()
            })
            .map(|(x, _stream)| assert!(x.is_none(), "should have emptied"));

        // add a timeout to receiver so if test is broken it doesn't take forever
        let recv =
            recv.on_timeout(300.millis().after_now(), || panic!("did not receive event in time!"));

        recv.await;
    }

    /// Tests that the event receiver can be taken, the stream read to the end,
    /// the receiver dropped, and then a new receiver gotten from taking the
    /// stream again.
    #[fasync::run_singlethreaded(test)]
    async fn receiver_can_be_taken_after_end_of_stream() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        // Send the event from the server
        let server = AsyncChannel::from_channel(server_end).unwrap();
        let (bytes, handles) = (&mut vec![], &mut vec![]);
        let header = TransactionHeader::new(0, 5, DynamicFlags::empty());
        encode_transaction(header, bytes, handles);
        server.write_etc(bytes, handles).expect("Server channel write failed");
        drop(server);

        // Create a block to make sure the first event receiver is dropped.
        // Creating the block is a bit of paranoia, because awaiting the
        // future moves the receiver anyway.
        {
            let recv = client
                .take_event_receiver()
                .into_future()
                .then(|(x, stream)| {
                    let x = x.expect("should contain one element");
                    let x = x.expect("fidl error");
                    let x: i32 =
                        decode_transaction_body::<_, false>(x).expect("failed to decode event");
                    assert_eq!(x, 55);
                    stream.into_future()
                })
                .map(|(x, _stream)| assert!(x.is_none(), "should have emptied"));

            // add a timeout to receiver so if test is broken it doesn't take forever
            let recv = recv
                .on_timeout(300.millis().after_now(), || panic!("did not receive event in time!"));

            recv.await;
        }

        // if we take the event stream again, we should be able to get the next
        // without a panic, but that should be none
        let mut c = client.take_event_receiver();
        assert!(
            c.next().await.is_none(),
            "receiver on closed channel should return none on first call"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn event_incompatible_format() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        // Send the event from the server
        let server = AsyncChannel::from_channel(server_end).unwrap();
        let (bytes, handles) = (&mut vec![], &mut vec![]);
        let header = TransactionHeader::new_full(
            0,
            5,
            &crate::encoding::Context {
                wire_format_version: crate::encoding::WireFormatVersion::V2,
            },
            DynamicFlags::empty(),
            0,
        );
        encode_transaction(header, bytes, handles);
        server.write_etc(bytes, handles).expect("Server channel write failed");
        drop(server);

        let mut event_receiver = client.take_event_receiver();
        let recv = event_receiver.next().map(|event| {
            assert_matches!(event, Some(Err(crate::Error::IncompatibleMagicNumber(0))))
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let recv =
            recv.on_timeout(300.millis().after_now(), || panic!("did not receive event in time!"));

        recv.await;
    }

    #[test]
    fn client_always_wakes_pending_futures() {
        let mut executor = fasync::TestExecutor::new().unwrap();

        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        let mut event_receiver = client.take_event_receiver();

        // first poll on a response
        let (response_waker, response_waker_count) = new_count_waker();
        let response_cx = &mut Context::from_waker(&response_waker);
        let mut response_txid = Txid(0);
        let mut response_future = client
            .send_raw_query(|tx_id, bytes, handles| {
                response_txid = tx_id;
                let header =
                    TransactionHeader::new(response_txid.as_raw_id(), 42, DynamicFlags::empty());
                encode_transaction(header, bytes, handles);
                Ok(())
            })
            .expect("Couldn't send query");
        assert!(response_future.poll_unpin(response_cx).is_pending());

        // then, poll on an event
        let (event_waker, event_waker_count) = new_count_waker();
        let event_cx = &mut Context::from_waker(&event_waker);
        assert!(event_receiver.poll_next_unpin(event_cx).is_pending());

        // at this point, nothing should have been woken
        assert_eq!(response_waker_count.get(), 0);
        assert_eq!(event_waker_count.get(), 0);

        // next, simulate an event coming in
        send_transaction(TransactionHeader::new(0, 5, DynamicFlags::empty()), &server_end);

        // get event loop to deliver readiness notifications to channels
        let _ = executor.run_until_stalled(&mut future::pending::<()>());

        // BOTH response_waker and event_waker should be woken, since the transaction
        // that arrived could be for either.
        assert_eq!(response_waker_count.get(), 1);
        assert_eq!(event_waker_count.get(), 1);
        let last_response_waker_count = response_waker_count.get();

        // we'll pretend event_waker was woken, and have that poll out the event
        assert!(event_receiver.poll_next_unpin(event_cx).is_ready());

        // next, simulate a response coming in
        send_transaction(
            TransactionHeader::new(response_txid.as_raw_id(), 42, DynamicFlags::empty()),
            &server_end,
        );

        // get event loop to deliver readiness notifications to channels
        let _ = executor.run_until_stalled(&mut future::pending::<()>());

        // response waker should have been woken again
        assert_eq!(response_waker_count.get(), last_response_waker_count + 1);
    }

    #[test]
    fn client_always_wakes_pending_futures_on_epitaph() {
        let mut executor = fasync::TestExecutor::new().unwrap();

        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        let mut event_receiver = client.take_event_receiver();

        // first poll on a response
        let (response1_waker, response1_waker_count) = new_count_waker();
        let response1_cx = &mut Context::from_waker(&response1_waker);
        let mut response1_future = client
            .send_raw_query(|tx_id, bytes, handles| {
                let header = TransactionHeader::new(tx_id.as_raw_id(), 42, DynamicFlags::empty());
                encode_transaction(header, bytes, handles);
                Ok(())
            })
            .expect("Couldn't send query");
        assert!(response1_future.poll_unpin(response1_cx).is_pending());

        // then, poll on an event
        let (event_waker, event_waker_count) = new_count_waker();
        let event_cx = &mut Context::from_waker(&event_waker);
        assert!(event_receiver.poll_next_unpin(event_cx).is_pending());

        // poll on another response
        let (response2_waker, response2_waker_count) = new_count_waker();
        let response2_cx = &mut Context::from_waker(&response2_waker);
        let mut response2_future = client
            .send_raw_query(|tx_id, bytes, handles| {
                let header = TransactionHeader::new(tx_id.as_raw_id(), 42, DynamicFlags::empty());
                encode_transaction(header, bytes, handles);
                Ok(())
            })
            .expect("Couldn't send query");
        assert!(response2_future.poll_unpin(response2_cx).is_pending());

        let wakers = vec![response1_waker_count, response2_waker_count, event_waker_count];

        // get event loop to deliver readiness notifications to channels
        let _ = executor.run_until_stalled(&mut future::pending::<()>());

        // at this point, nothing should have been woken
        assert_eq!(0, wakers.iter().fold(0, |acc, x| acc + x.get()));

        // next, simulate an epitaph without closing
        epitaph::write_epitaph_impl(&server_end, zx_status::Status::UNAVAILABLE)
            .expect("wrote epitaph");

        // get event loop to deliver readiness notifications to channels
        let _ = executor.run_until_stalled(&mut future::pending::<()>());

        // All the wakers should be woken up because the channel is ready to read, and the message
        // could be for any of them.
        for wake_count in &wakers {
            assert_eq!(wake_count.get(), 1);
        }

        // pretend that response1 woke and poll that to completion.
        assert_matches!(
            response1_future.poll_unpin(response1_cx),
            Poll::Ready(Err(crate::Error::ClientChannelClosed {
                status: zx_status::Status::UNAVAILABLE,
                protocol_name: "test_protocol"
            }))
        );

        // get event loop to deliver readiness notifications to channels
        let _ = executor.run_until_stalled(&mut future::pending::<()>());

        // response1 will have woken all the wakers a second time, because epitaphs wake everyone.
        // TODO: we don't actually need to do this, I think.
        for wake_count in &wakers {
            assert_eq!(wake_count.get(), 2);
        }

        // poll response2 to completion.
        assert_matches!(
            response2_future.poll_unpin(response2_cx),
            Poll::Ready(Err(crate::Error::ClientChannelClosed {
                status: zx_status::Status::UNAVAILABLE,
                protocol_name: "test_protocol"
            }))
        );

        // poll the event stream to completion.
        assert!(event_receiver.poll_next_unpin(event_cx).is_ready());
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_allows_take_event_stream_even_if_event_delivered() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        // first simulate an event coming in, even though nothing has polled
        send_transaction(TransactionHeader::new(0, 5, DynamicFlags::empty()), &server_end);

        // next, poll on a response
        let (response_waker, _response_waker_count) = new_count_waker();
        let response_cx = &mut Context::from_waker(&response_waker);
        let mut response_future =
            client.send_query::<u8, u8, false, false>(&mut 55, 42, DynamicFlags::empty());
        assert!(response_future.poll_unpin(response_cx).is_pending());

        // then, make sure we can still take the event receiver without panicking
        let mut _event_receiver = client.take_event_receiver();
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_reports_epitaph_from_all_actions() {
        #[derive(Debug, PartialEq)]
        enum Action {
            SendMsg,    // send a one-way message
            SendQuery,  // send a two-way message and wait for the response
            CheckQuery, // send a two-way message and just call .check()
            RecvEvent,  // wait to receive an event
        }
        use Action::*;
        // Test all permutations of two actions. The first one reports an
        // epitaph, and then second one re-reports the epitaph.
        for two_actions in &[
            [SendMsg, SendMsg],
            [SendMsg, SendQuery],
            [SendMsg, CheckQuery],
            [SendMsg, RecvEvent],
            [SendQuery, SendMsg],
            [SendQuery, SendQuery],
            [SendQuery, CheckQuery],
            [SendQuery, RecvEvent],
            [CheckQuery, SendMsg],
            [CheckQuery, SendQuery],
            [CheckQuery, CheckQuery],
            [CheckQuery, RecvEvent],
            [RecvEvent, SendMsg],
            [RecvEvent, SendQuery],
            [RecvEvent, CheckQuery],
            // No [RecvEvent, RecvEvent] because it behaves differently: after
            // reporting an epitaph, the next call returns None.
        ] {
            let (client_end, server_end) = zx::Channel::create().unwrap();
            let client_end = AsyncChannel::from_channel(client_end).unwrap();
            let client = Client::new(client_end, "test_protocol");

            // Immediately close the FIDL channel with an epitaph.
            let server_end = AsyncChannel::from_channel(server_end).unwrap();
            server_end
                .close_with_epitaph(zx_status::Status::UNAVAILABLE)
                .expect("failed to write epitaph");

            let mut event_receiver = client.take_event_receiver();

            // Assert that each action reports the epitaph.
            for (index, action) in two_actions.iter().enumerate() {
                let err = match action {
                    SendMsg => client
                        .send::<u8, false>(
                            &mut SEND_DATA.clone(),
                            SEND_ORDINAL,
                            DynamicFlags::empty(),
                        )
                        .err(),
                    SendQuery => client
                        .send_query::<u8, u8, false, false>(
                            &mut SEND_DATA.clone(),
                            SEND_ORDINAL,
                            DynamicFlags::empty(),
                        )
                        .await
                        .err(),
                    CheckQuery => client
                        .send_query::<u8, u8, false, false>(
                            &mut SEND_DATA.clone(),
                            SEND_ORDINAL,
                            DynamicFlags::empty(),
                        )
                        .check()
                        .err(),
                    RecvEvent => event_receiver.next().await.unwrap().err(),
                };
                // TODO(fxbug.dev/72968): Switch to built-in assert_matches once
                // stabilized, and just provide "index: {:?}, actions: {:?}".
                match err {
                    Some(crate::Error::ClientChannelClosed {
                        status: zx_status::Status::UNAVAILABLE,
                        protocol_name: "test_protocol",
                    }) => (),
                    Some(err) => panic!(
                        "expected epitaph, got {:#?}.\nindex: {:?}, two_actions: {:?}",
                        err, index, two_actions
                    ),
                    None => panic!(
                        "expected epitaph, but it succeeded unexpectedly.\nindex: {:?}, two_actions: {:?}",
                        index, two_actions
                    )
                }
            }

            // If we got the epitaph from RecvEvent, the next should return None.
            if two_actions.contains(&RecvEvent) {
                assert_matches!(event_receiver.next().await, None);
            }
        }
    }

    #[test]
    fn client_query_result_check() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        let server = AsyncChannel::from_channel(server_end).unwrap();

        // Sending works, and checking when a message successfully sends returns itself.
        let active_fut = client.send_query::<u8, u8, false, false>(
            &mut SEND_DATA.clone(),
            SEND_ORDINAL,
            DynamicFlags::empty(),
        );

        let mut checked_fut = active_fut.check().expect("failed to check future");

        // Should be able to complete the query even after checking.
        let mut buffer = MessageBufEtc::new();
        executor.run_singlethreaded(server.recv_etc_msg(&mut buffer)).expect("failed to recv msg");
        let two_way_tx_id = 1u8;
        assert_eq!(buffer.bytes(), expected_sent_bytes(two_way_tx_id));

        let (bytes, handles) = (&mut vec![], &mut vec![]);
        let header = TransactionHeader::new(two_way_tx_id as u32, 42, DynamicFlags::empty());
        encode_transaction(header, bytes, handles);
        server.write_etc(bytes, handles).expect("Server channel write failed");

        executor
            .run_singlethreaded(&mut checked_fut)
            .map(|x| assert_eq!(x, SEND_DATA))
            .unwrap_or_else(|e| panic!("fidl error: {:?}", e));

        // Close the server channel, meaning the next query will fail before it even starts.
        drop(server);

        let query_fut = client.send_query::<u8, u8, false, false>(
            &mut SEND_DATA.clone(),
            SEND_ORDINAL,
            DynamicFlags::empty(),
        );

        // This should be an error, because the server end is closed.
        query_fut.check().expect_err("Didn't make an error on check");
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_into_channel() {
        // This test doesn't actually do any async work, but the fuchsia
        // executor must be set up in order to create the channel.
        let (client_end, _server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        assert!(client.into_channel().is_ok());
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_into_channel_outstanding_messages() {
        // This test doesn't actually do any async work, but the fuchsia
        // executor must be set up in order to create the channel.
        let (client_end, _server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        {
            // Create a send future to insert a message interest but drop it
            // before a response can be received.
            let _sender = client.send_query::<u8, u8, false, false>(
                &mut SEND_DATA.clone(),
                SEND_ORDINAL,
                DynamicFlags::empty(),
            );
        }

        assert!(client.into_channel().is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_into_channel_outstanding_messages_get_received() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = AsyncChannel::from_channel(client_end).unwrap();
        let client = Client::new(client_end, "test_protocol");

        let server = AsyncChannel::from_channel(server_end).unwrap();
        let mut buffer = MessageBufEtc::new();
        let receiver = async move {
            server.recv_etc_msg(&mut buffer).await.expect("failed to recv msg");
            let two_way_tx_id = 1u8;
            assert_eq!(buffer.bytes(), expected_sent_bytes(two_way_tx_id));

            let (bytes, handles) = (&mut vec![], &mut vec![]);
            let header = TransactionHeader::new(two_way_tx_id as u32, 42, DynamicFlags::empty());
            encode_transaction(header, bytes, handles);
            server.write_etc(bytes, handles).expect("Server channel write failed");
        };

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver
            .on_timeout(300.millis().after_now(), || panic!("did not receiver message in time!"));

        let sender = client
            .send_query::<u8, u8, false, false>(
                &mut SEND_DATA.clone(),
                SEND_ORDINAL,
                DynamicFlags::empty(),
            )
            .map_ok(|x| assert_eq!(x, SEND_DATA))
            .unwrap_or_else(|e| panic!("fidl error: {:?}", e));

        // add a timeout to receiver so if test is broken it doesn't take forever
        let sender = sender
            .on_timeout(300.millis().after_now(), || panic!("did not receive response in time!"));

        let ((), ()) = join!(receiver, sender);

        assert!(client.into_channel().is_ok());
    }
}
