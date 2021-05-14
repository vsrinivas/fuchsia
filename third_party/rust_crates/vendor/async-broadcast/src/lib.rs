//! Async broadcast channel
//!
//! An async multi-producer multi-consumer broadcast channel, where each consumer gets a clone of every
//! message sent on the channel. For obvious reasons, the channel can only be used to broadcast types
//! that implement [`Clone`].
//!
//! A channel has the [`Sender`] and [`Receiver`] side. Both sides are cloneable and can be shared
//! among multiple threads.
//!
//! When all `Sender`s or all `Receiver`s are dropped, the channel becomes closed. When a channel is
//! closed, no more messages can be sent, but remaining messages can still be received.
//!
//! The channel can also be closed manually by calling [`Sender::close()`] or [`Receiver::close()`].
//!
//! ## Examples
//!
//! ```rust
//! use async_broadcast::{broadcast, TryRecvError};
//! use futures_lite::{future::block_on, stream::StreamExt};
//!
//! block_on(async move {
//!     let (s1, mut r1) = broadcast(2);
//!     let s2 = s1.clone();
//!     let mut r2 = r1.clone();
//!
//!     // Send 2 messages from two different senders.
//!     s1.broadcast(7).await.unwrap();
//!     s2.broadcast(8).await.unwrap();
//!
//!     // Channel is now at capacity so sending more messages will result in an error.
//!     assert!(s2.try_broadcast(9).unwrap_err().is_full());
//!     assert!(s1.try_broadcast(10).unwrap_err().is_full());
//!
//!     // We can use `recv` method of the `Stream` implementation to receive messages.
//!     assert_eq!(r1.next().await.unwrap(), 7);
//!     assert_eq!(r1.recv().await.unwrap(), 8);
//!     assert_eq!(r2.next().await.unwrap(), 7);
//!     assert_eq!(r2.recv().await.unwrap(), 8);
//!
//!     // All receiver got all messages so channel is now empty.
//!     assert_eq!(r1.try_recv(), Err(TryRecvError::Empty));
//!     assert_eq!(r2.try_recv(), Err(TryRecvError::Empty));
//!
//!     // Drop both senders, which closes the channel.
//!     drop(s1);
//!     drop(s2);
//!
//!     assert_eq!(r1.try_recv(), Err(TryRecvError::Closed));
//!     assert_eq!(r2.try_recv(), Err(TryRecvError::Closed));
//! })
//! ```

#![forbid(unsafe_code, future_incompatible, rust_2018_idioms)]
#![deny(missing_debug_implementations, nonstandard_style)]
#![warn(missing_docs, missing_doc_code_examples, unreachable_pub)]

#[cfg(doctest)]
mod doctests {
    doc_comment::doctest!("../README.md");
}

use std::collections::VecDeque;
use std::error;
use std::fmt;
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};

use event_listener::{Event, EventListener};
use futures_core::stream::Stream;

/// Create a new broadcast channel.
///
/// The created channel has space to hold at most `cap` messages at a time.
///
/// # Panics
///
/// Capacity must be a positive number. If `cap` is zero, this function will panic.
///
/// # Examples
///
/// ```
/// # futures_lite::future::block_on(async {
/// use async_broadcast::{broadcast, TryRecvError, TrySendError};
///
/// let (s, mut r1) = broadcast(1);
/// let mut r2 = r1.clone();
///
/// assert_eq!(s.broadcast(10).await, Ok(()));
/// assert_eq!(s.try_broadcast(20), Err(TrySendError::Full(20)));
///
/// assert_eq!(r1.recv().await, Ok(10));
/// assert_eq!(r2.recv().await, Ok(10));
/// assert_eq!(r1.try_recv(), Err(TryRecvError::Empty));
/// assert_eq!(r2.try_recv(), Err(TryRecvError::Empty));
/// # });
/// ```
pub fn broadcast<T>(cap: usize) -> (Sender<T>, Receiver<T>) {
    assert!(cap > 0, "capacity cannot be zero");

    let inner = Arc::new(Mutex::new(Inner {
        queue: VecDeque::with_capacity(cap),
        receiver_count: 1,
        sender_count: 1,
        send_count: 0,
        is_closed: false,
        send_ops: Event::new(),
        recv_ops: Event::new(),
    }));
    let s = Sender {
        inner: inner.clone(),
        capacity: cap,
    };
    let r = Receiver {
        inner,
        capacity: cap,
        recv_count: 0,
        listener: None,
    };
    (s, r)
}

#[derive(Debug)]
struct Inner<T> {
    queue: VecDeque<(T, usize)>,
    receiver_count: usize,
    sender_count: usize,
    send_count: usize,

    is_closed: bool,

    /// Send operations waiting while the channel is full.
    send_ops: Event,

    /// Receive operations waiting while the channel is empty and not closed.
    recv_ops: Event,
}

impl<T> Inner<T> {
    /// Closes the channel and notifies all waiting operations.
    ///
    /// Returns `true` if this call has closed the channel and it was not closed already.
    fn close(&mut self) -> bool {
        if self.is_closed {
            return false;
        }

        self.is_closed = true;
        // Notify all waiting senders and receivers.
        self.send_ops.notify(usize::MAX);
        self.recv_ops.notify(usize::MAX);

        true
    }
}

/// The sending side of the broadcast channel.
///
/// Senders can be cloned and shared among threads. When all senders associated with a channel are
/// dropped, the channel becomes closed.
///
/// The channel can also be closed manually by calling [`Sender::close()`].
#[derive(Debug)]
pub struct Sender<T> {
    inner: Arc<Mutex<Inner<T>>>,
    capacity: usize,
}

impl<T> Sender<T> {
    /// Returns the channel capacity.
    ///
    /// # Examples
    ///
    /// ```
    /// use async_broadcast::broadcast;
    ///
    /// let (s, r) = broadcast::<i32>(5);
    /// assert_eq!(s.capacity(), 5);
    /// ```
    pub fn capacity(&self) -> usize {
        self.capacity
    }
}

impl<T: Clone> Sender<T> {
    /// Broadcasts a message on the channel.
    ///
    /// If the channel is full, this method waits until there is space for a message.
    ///
    /// If the channel is closed, this method returns an error.
    ///
    /// # Examples
    ///
    /// ```
    /// # futures_lite::future::block_on(async {
    /// use async_broadcast::{broadcast, SendError};
    ///
    /// let (s, r) = broadcast(1);
    ///
    /// assert_eq!(s.broadcast(1).await, Ok(()));
    /// drop(r);
    /// assert_eq!(s.broadcast(2).await, Err(SendError(2)));
    /// # });
    /// ```
    pub fn broadcast(&self, msg: T) -> Send<'_, T> {
        Send {
            sender: self,
            listener: None,
            msg: Some(msg),
        }
    }

    /// Attempts to broadcast a message on the channel.
    ///
    /// If the channel is full or closed, this method returns an error.
    ///
    /// # Examples
    ///
    /// ```
    /// use async_broadcast::{broadcast, TrySendError};
    ///
    /// let (s, r) = broadcast(1);
    ///
    /// assert_eq!(s.try_broadcast(1), Ok(()));
    /// assert_eq!(s.try_broadcast(2), Err(TrySendError::Full(2)));
    ///
    /// drop(r);
    /// assert_eq!(s.try_broadcast(3), Err(TrySendError::Closed(3)));
    /// ```
    pub fn try_broadcast(&self, msg: T) -> Result<(), TrySendError<T>> {
        let mut inner = self.inner.lock().unwrap();
        if inner.is_closed {
            return Err(TrySendError::Closed(msg));
        } else if inner.queue.len() == self.capacity {
            return Err(TrySendError::Full(msg));
        }
        let receiver_count = inner.receiver_count;
        inner.queue.push_back((msg, receiver_count));
        inner.send_count += 1;

        // Notify all awaiting receive operations.
        inner.recv_ops.notify(usize::MAX);

        Ok(())
    }

    /// Closes the channel.
    ///
    /// Returns `true` if this call has closed the channel and it was not closed already.
    ///
    /// The remaining messages can still be received.
    ///
    /// # Examples
    ///
    /// ```
    /// # futures_lite::future::block_on(async {
    /// use async_broadcast::{broadcast, RecvError};
    ///
    /// let (s, mut r) = broadcast(1);
    /// s.broadcast(1).await.unwrap();
    /// assert!(s.close());
    ///
    /// assert_eq!(r.recv().await.unwrap(), 1);
    /// assert_eq!(r.recv().await, Err(RecvError));
    /// # });
    /// ```
    pub fn close(&self) -> bool {
        self.inner.lock().unwrap().close()
    }
}

impl<T> Drop for Sender<T> {
    fn drop(&mut self) {
        let mut inner = self.inner.lock().unwrap();
        inner.sender_count -= 1;

        if inner.sender_count == 0 {
            inner.close();
        }
    }
}

impl<T> Clone for Sender<T> {
    fn clone(&self) -> Self {
        self.inner.lock().unwrap().sender_count += 1;

        Sender {
            inner: self.inner.clone(),
            capacity: self.capacity,
        }
    }
}

/// The receiving side of a channel.
#[derive(Debug)]
pub struct Receiver<T> {
    inner: Arc<Mutex<Inner<T>>>,
    capacity: usize,
    recv_count: usize,

    /// Listens for a send or close event to unblock this stream.
    listener: Option<EventListener>,
}

impl<T: Clone> Receiver<T> {
    /// Receives a message from the channel.
    ///
    /// If the channel is empty, this method waits until there is a message.
    ///
    /// If the channel is closed, this method receives a message or returns an error if there are
    /// no more messages.
    ///
    /// # Examples
    ///
    /// ```
    /// # futures_lite::future::block_on(async {
    /// use async_broadcast::{broadcast, RecvError};
    ///
    /// let (s, mut r1) = broadcast(1);
    /// let mut r2 = r1.clone();
    ///
    /// assert_eq!(s.broadcast(1).await, Ok(()));
    /// drop(s);
    ///
    /// assert_eq!(r1.recv().await, Ok(1));
    /// assert_eq!(r1.recv().await, Err(RecvError));
    /// assert_eq!(r2.recv().await, Ok(1));
    /// assert_eq!(r2.recv().await, Err(RecvError));
    /// # });
    /// ```
    pub fn recv(&mut self) -> Recv<'_, T> {
        Recv {
            receiver: self,
            listener: None,
        }
    }

    /// Attempts to receive a message from the channel.
    ///
    /// If the channel is empty or closed, this method returns an error.
    ///
    /// # Examples
    ///
    /// ```
    /// # futures_lite::future::block_on(async {
    /// use async_broadcast::{broadcast, TryRecvError};
    ///
    /// let (s, mut r1) = broadcast(1);
    /// let mut r2 = r1.clone();
    /// assert_eq!(s.broadcast(1).await, Ok(()));
    ///
    /// assert_eq!(r1.try_recv(), Ok(1));
    /// assert_eq!(r1.try_recv(), Err(TryRecvError::Empty));
    /// assert_eq!(r2.try_recv(), Ok(1));
    /// assert_eq!(r2.try_recv(), Err(TryRecvError::Empty));
    ///
    /// drop(s);
    /// assert_eq!(r1.try_recv(), Err(TryRecvError::Closed));
    /// assert_eq!(r2.try_recv(), Err(TryRecvError::Closed));
    /// # });
    /// ```
    pub fn try_recv(&mut self) -> Result<T, TryRecvError> {
        let mut inner = self.inner.lock().unwrap();
        let msg_count = inner.send_count - self.recv_count;
        if msg_count == 0 {
            if inner.is_closed {
                return Err(TryRecvError::Closed);
            } else {
                return Err(TryRecvError::Empty);
            }
        }
        let len = inner.queue.len();
        let msg = inner.queue[len - msg_count].0.clone();
        inner.queue[len - msg_count].1 -= 1;
        if inner.queue[len - msg_count].1 == 0 {
            inner.queue.pop_front();

            // Notify 1 awaiting senders that there is now room. If there is still room in the
            // queue, the notified operation will notify another awaiting sender.
            inner.send_ops.notify(1);
        }
        self.recv_count += 1;
        Ok(msg)
    }

    /// Returns the channel capacity.
    ///
    /// # Examples
    ///
    /// ```
    /// use async_broadcast::broadcast;
    ///
    /// let (s, r) = broadcast::<i32>(5);
    /// assert_eq!(r.capacity(), 5);
    /// ```
    pub fn capacity(&self) -> usize {
        self.capacity
    }

    /// Closes the channel.
    ///
    /// Returns `true` if this call has closed the channel and it was not closed already.
    ///
    /// The remaining messages can still be received.
    ///
    /// # Examples
    ///
    /// ```
    /// # futures_lite::future::block_on(async {
    /// use async_broadcast::{broadcast, RecvError};
    ///
    /// let (s, mut r) = broadcast(1);
    /// s.broadcast(1).await.unwrap();
    /// assert!(s.close());
    ///
    /// assert_eq!(r.recv().await.unwrap(), 1);
    /// assert_eq!(r.recv().await, Err(RecvError));
    /// # });
    /// ```
    pub fn close(&self) -> bool {
        self.inner.lock().unwrap().close()
    }
}

impl<T> Drop for Receiver<T> {
    fn drop(&mut self) {
        let mut inner = self.inner.lock().unwrap();
        let msg_count = inner.send_count - self.recv_count;
        let len = inner.queue.len();

        for i in len - msg_count..len {
            inner.queue[i].1 -= 1;
        }
        let mut poped = false;
        while let Some((_, 0)) = inner.queue.front() {
            inner.queue.pop_front();
            if !poped {
                poped = true;
            }
        }

        if poped {
            // Notify 1 awaiting senders that there is now room. If there is still room in the
            // queue, the notified operation will notify another awaiting sender.
            inner.send_ops.notify(1);
        }
        inner.receiver_count -= 1;

        if inner.receiver_count == 0 {
            inner.close();
        }
    }
}

impl<T> Clone for Receiver<T> {
    fn clone(&self) -> Self {
        let mut inner = self.inner.lock().unwrap();
        inner.receiver_count += 1;
        Receiver {
            inner: self.inner.clone(),
            capacity: self.capacity,
            recv_count: inner.send_count,
            listener: None,
        }
    }
}

impl<T: Clone> Stream for Receiver<T> {
    type Item = T;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        loop {
            // If this stream is listening for events, first wait for a notification.
            if let Some(listener) = self.listener.as_mut() {
                futures_core::ready!(Pin::new(listener).poll(cx));
                self.listener = None;
            }

            loop {
                // Attempt to receive a message.
                match self.try_recv() {
                    Ok(msg) => {
                        // The stream is not blocked on an event - drop the listener.
                        self.listener = None;
                        return Poll::Ready(Some(msg));
                    }
                    Err(TryRecvError::Closed) => {
                        // The stream is not blocked on an event - drop the listener.
                        self.listener = None;
                        return Poll::Ready(None);
                    }
                    Err(TryRecvError::Empty) => {}
                }

                // Receiving failed - now start listening for notifications or wait for one.
                match self.listener.as_mut() {
                    None => {
                        // Start listening and then try receiving again.
                        self.listener = {
                            let inner = self.inner.lock().unwrap();

                            Some(inner.recv_ops.listen())
                        };
                    }
                    Some(_) => {
                        // Go back to the outer loop to poll the listener.
                        break;
                    }
                }
            }
        }
    }
}

impl<T: Clone> futures_core::stream::FusedStream for Receiver<T> {
    fn is_terminated(&self) -> bool {
        let inner = self.inner.lock().unwrap();

        inner.is_closed && inner.queue.is_empty()
    }
}

/// An error returned from [`Sender::broadcast()`].
///
/// Received because the channel is closed.
#[derive(PartialEq, Eq, Clone, Copy)]
pub struct SendError<T>(pub T);

impl<T> SendError<T> {
    /// Unwraps the message that couldn't be sent.
    pub fn into_inner(self) -> T {
        self.0
    }
}

impl<T> error::Error for SendError<T> {}

impl<T> fmt::Debug for SendError<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "SendError(..)")
    }
}

impl<T> fmt::Display for SendError<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "sending into a closed channel")
    }
}

/// An error returned from [`Sender::try_broadcast()`].
#[derive(PartialEq, Eq, Clone, Copy)]
pub enum TrySendError<T> {
    /// The channel is full but not closed.
    Full(T),

    /// The channel is closed.
    Closed(T),
}

impl<T> TrySendError<T> {
    /// Unwraps the message that couldn't be sent.
    pub fn into_inner(self) -> T {
        match self {
            TrySendError::Full(t) => t,
            TrySendError::Closed(t) => t,
        }
    }

    /// Returns `true` if the channel is full but not closed.
    pub fn is_full(&self) -> bool {
        match self {
            TrySendError::Full(_) => true,
            TrySendError::Closed(_) => false,
        }
    }

    /// Returns `true` if the channel is closed.
    pub fn is_closed(&self) -> bool {
        match self {
            TrySendError::Full(_) => false,
            TrySendError::Closed(_) => true,
        }
    }
}

impl<T> error::Error for TrySendError<T> {}

impl<T> fmt::Debug for TrySendError<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            TrySendError::Full(..) => write!(f, "Full(..)"),
            TrySendError::Closed(..) => write!(f, "Closed(..)"),
        }
    }
}

impl<T> fmt::Display for TrySendError<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            TrySendError::Full(..) => write!(f, "sending into a full channel"),
            TrySendError::Closed(..) => write!(f, "sending into a closed channel"),
        }
    }
}

/// An error returned from [`Receiver::recv()`].
///
/// Received because the channel is empty and closed.
#[derive(PartialEq, Eq, Clone, Copy, Debug)]
pub struct RecvError;

impl error::Error for RecvError {}

impl fmt::Display for RecvError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "receiving from an empty and closed channel")
    }
}

/// An error returned from [`Receiver::try_recv()`].
#[derive(PartialEq, Eq, Clone, Copy, Debug)]
pub enum TryRecvError {
    /// The channel is empty but not closed.
    Empty,

    /// The channel is empty and closed.
    Closed,
}

impl TryRecvError {
    /// Returns `true` if the channel is empty but not closed.
    pub fn is_empty(&self) -> bool {
        match self {
            TryRecvError::Empty => true,
            TryRecvError::Closed => false,
        }
    }

    /// Returns `true` if the channel is empty and closed.
    pub fn is_closed(&self) -> bool {
        match self {
            TryRecvError::Empty => false,
            TryRecvError::Closed => true,
        }
    }
}

impl error::Error for TryRecvError {}

impl fmt::Display for TryRecvError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            TryRecvError::Empty => write!(f, "receiving from an empty channel"),
            TryRecvError::Closed => write!(f, "receiving from an empty and closed channel"),
        }
    }
}

/// A future returned by [`Sender::broadcast()`].
#[derive(Debug)]
#[must_use = "futures do nothing unless .awaited"]
pub struct Send<'a, T> {
    sender: &'a Sender<T>,
    listener: Option<EventListener>,
    msg: Option<T>,
}

impl<'a, T> Unpin for Send<'a, T> {}

impl<'a, T: Clone> Future for Send<'a, T> {
    type Output = Result<(), SendError<T>>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut this = Pin::new(self);

        loop {
            let msg = this.msg.take().unwrap();

            // Attempt to send a message.
            match this.sender.try_broadcast(msg) {
                Ok(()) => {
                    let inner = this.sender.inner.lock().unwrap();

                    if inner.queue.len() < this.sender.capacity() {
                        // Not full still, so notify the next awaiting sender.
                        inner.send_ops.notify(1);
                    }

                    return Poll::Ready(Ok(()));
                }
                Err(TrySendError::Closed(msg)) => return Poll::Ready(Err(SendError(msg))),
                Err(TrySendError::Full(m)) => this.msg = Some(m),
            }

            // Sending failed - now start listening for notifications or wait for one.
            match &mut this.listener {
                None => {
                    // Start listening and then try sending again.
                    let inner = this.sender.inner.lock().unwrap();
                    this.listener = Some(inner.send_ops.listen());
                }
                Some(l) => {
                    // Wait for a notification.
                    match Pin::new(l).poll(cx) {
                        Poll::Ready(_) => {
                            this.listener = None;
                            continue;
                        }

                        Poll::Pending => return Poll::Pending,
                    }
                }
            }
        }
    }
}

/// A future returned by [`Receiver::recv()`].
#[derive(Debug)]
#[must_use = "futures do nothing unless .awaited"]
pub struct Recv<'a, T> {
    receiver: &'a mut Receiver<T>,
    listener: Option<EventListener>,
}

impl<'a, T> Unpin for Recv<'a, T> {}

impl<'a, T: Clone> Future for Recv<'a, T> {
    type Output = Result<T, RecvError>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut this = Pin::new(self);

        loop {
            // Attempt to receive a message.
            match this.receiver.try_recv() {
                Ok(msg) => return Poll::Ready(Ok(msg)),
                Err(TryRecvError::Closed) => return Poll::Ready(Err(RecvError)),
                Err(TryRecvError::Empty) => {}
            }

            // Receiving failed - now start listening for notifications or wait for one.
            match &mut this.listener {
                None => {
                    // Start listening and then try receiving again.
                    this.listener = {
                        let inner = this.receiver.inner.lock().unwrap();

                        Some(inner.recv_ops.listen())
                    };
                }
                Some(l) => {
                    // Wait for a notification.
                    match Pin::new(l).poll(cx) {
                        Poll::Ready(_) => {
                            this.listener = None;
                            continue;
                        }

                        Poll::Pending => return Poll::Pending,
                    }
                }
            }
        }
    }
}
