// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::executor::{EHandle, PacketReceiver, ReceiverRegistration};
use fuchsia_zircon::{self as zx, AsHandleRef, Signals};
use futures::io::{self, AsyncRead, AsyncWrite};
use futures::{
    future::poll_fn,
    ready,
    stream::Stream,
    task::{AtomicWaker, Context},
};
use std::fmt;
use std::pin::Pin;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
use std::task::Poll;

pub struct SocketPacketReceiver {
    signals: AtomicU32,
    read_task: AtomicWaker,
    write_task: AtomicWaker,
}

impl PacketReceiver for SocketPacketReceiver {
    fn receive_packet(&self, packet: zx::Packet) {
        let observed = if let zx::PacketContents::SignalOne(p) = packet.contents() {
            p.observed()
        } else {
            return;
        };

        let old =
            Signals::from_bits(self.signals.fetch_or(observed.bits(), Ordering::SeqCst)).unwrap();

        let became_closed = observed.contains(Signals::SOCKET_PEER_CLOSED)
            && !old.contains(Signals::SOCKET_PEER_CLOSED);

        if observed.contains(Signals::SOCKET_READABLE) && !old.contains(Signals::SOCKET_READABLE)
            || became_closed
        {
            self.read_task.wake();
        }
        if observed.contains(Signals::SOCKET_WRITABLE) && !old.contains(Signals::SOCKET_WRITABLE)
            || became_closed
        {
            self.write_task.wake();
        }
    }
}

impl SocketPacketReceiver {
    fn new(signals: AtomicU32, read_task: AtomicWaker, write_task: AtomicWaker) -> Self {
        Self { signals, read_task, write_task }
    }
}

/// An I/O object representing a `Socket`.
pub struct Socket {
    handle: Arc<zx::Socket>,
    receiver: ReceiverRegistration<SocketPacketReceiver>,
}

impl AsRef<zx::Socket> for Socket {
    fn as_ref(&self) -> &zx::Socket {
        &self.handle
    }
}

impl AsHandleRef for Socket {
    fn as_handle_ref(&self) -> zx::HandleRef<'_> {
        self.handle.as_handle_ref()
    }
}

impl Socket {
    /// Create a new `Socket` from a previously-created zx::Socket.
    pub fn from_socket(handle: zx::Socket) -> Result<Self, zx::Status> {
        Self::from_socket_arc_unchecked(Arc::new(handle))
    }
}

impl Socket {
    /// Creates a new `Socket` from a previously-created `zx::Socket`.
    fn from_socket_arc_unchecked(handle: Arc<zx::Socket>) -> Result<Self, zx::Status> {
        let ehandle = EHandle::local();

        // Optimistically assume that the handle is readable and writable.
        // Reads and writes will be attempted before queueing a packet.
        // This makes handles slightly faster to read/write the first time
        // they're accessed after being created, provided they start off as
        // readable or writable. In return, there will be an extra wasted
        // syscall per read/write if the handle is not readable or writable.
        let receiver = ehandle.register_receiver(Arc::new(SocketPacketReceiver::new(
            AtomicU32::new(Signals::SOCKET_READABLE.bits() | Signals::SOCKET_WRITABLE.bits()),
            AtomicWaker::new(),
            AtomicWaker::new(),
        )));

        let socket = Self { handle, receiver };

        // Make sure we get notifications when the handle closes.
        socket.schedule_packet(Signals::SOCKET_PEER_CLOSED)?;

        Ok(socket)
    }

    /// Tests if the resource currently has either the provided `signal`
    /// or the OBJECT_PEER_CLOSED signal set.
    ///
    /// Returns `true` if the CLOSED signal was set.
    fn poll_signal_or_closed(
        &self,
        cx: &mut Context<'_>,
        task: &AtomicWaker,
        signal: zx::Signals,
    ) -> Poll<Result<bool, zx::Status>> {
        let signals = zx::Signals::from_bits_truncate(self.receiver.signals.load(Ordering::SeqCst));
        let was_closed = signals.contains(zx::Signals::OBJECT_PEER_CLOSED);
        let was_signal = signals.contains(signal);
        if was_closed || was_signal {
            Poll::Ready(Ok(was_closed))
        } else {
            self.need_signal(cx, task, signal, was_closed)?;
            Poll::Pending
        }
    }

    /// Test whether this socket is ready to be read or not.
    ///
    /// If the socket is *not* readable then the current task is scheduled to
    /// get a notification when the socket does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is readable again.
    ///
    /// Returns `true` if the CLOSED signal was set.
    pub fn poll_read_task(&self, cx: &mut Context<'_>) -> Poll<Result<bool, zx::Status>> {
        self.poll_signal_or_closed(cx, &self.receiver.read_task, Signals::SOCKET_READABLE)
    }

    /// Test whether this socket is ready to be written to or not.
    ///
    /// If the socket is *not* writable then the current task is scheduled to
    /// get a notification when the socket does become writable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the socket is writable again.
    ///
    /// Returns `true` if the CLOSED signal was set.
    pub fn poll_write_task(&self, cx: &mut Context<'_>) -> Poll<Result<bool, zx::Status>> {
        self.poll_signal_or_closed(cx, &self.receiver.write_task, Signals::SOCKET_WRITABLE)
    }

    fn need_signal(
        &self,
        cx: &mut Context<'_>,
        task: &AtomicWaker,
        signal: zx::Signals,
        clear_closed: bool,
    ) -> Result<(), zx::Status> {
        crate::executor::need_signal(
            cx,
            task,
            &self.receiver.signals,
            signal,
            clear_closed,
            self.handle.as_handle_ref(),
            self.receiver.port(),
            self.receiver.key(),
        )
    }

    /// Arranges for the current task to receive a notification when a
    /// "readable" signal arrives.
    ///
    /// `clear_closed` indicates that we previously mistakenly thought
    /// the channel was closed due to a false signal, and we should
    /// now reset the CLOSED bit. This value should often be passed in directly
    /// from the output of `poll_read`.
    pub fn need_read(&self, cx: &mut Context<'_>, clear_closed: bool) -> Result<(), zx::Status> {
        self.need_signal(cx, &self.receiver.read_task, Signals::SOCKET_READABLE, clear_closed)
    }

    /// Arranges for the current task to receive a notification when a
    /// "writable" signal arrives.
    ///
    /// `clear_closed` indicates that we previously mistakenly thought
    /// the channel was closed due to a false signal, and we should
    /// now reset the CLOSED bit. This value should often be passed in directly
    /// from the output of `poll_write`.
    pub fn need_write(&self, cx: &mut Context<'_>, clear_closed: bool) -> Result<(), zx::Status> {
        self.need_signal(cx, &self.receiver.write_task, Signals::SOCKET_WRITABLE, clear_closed)
    }

    fn schedule_packet(&self, signals: Signals) -> Result<(), zx::Status> {
        crate::executor::schedule_packet(
            self.handle.as_handle_ref(),
            self.receiver.port(),
            self.receiver.key(),
            signals,
        )
    }

    // Private helper for reading without `&mut` self.
    // This is used in the impls of `Read` for `Socket` and `&Socket`.
    fn read_nomut(&self, buf: &mut [u8], cx: &mut Context<'_>) -> Poll<Result<usize, zx::Status>> {
        let clear_closed = ready!(self.poll_read_task(cx))?;
        let res = self.handle.read(buf);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.need_read(cx, clear_closed)?;
            return Poll::Pending;
        }
        if res == Err(zx::Status::PEER_CLOSED) {
            return Poll::Ready(Ok(0));
        }
        Poll::Ready(res)
    }

    // Private helper for writing without `&mut` self.
    // This is used in the impls of `Write` for `Socket` and `&Socket`.
    fn write_nomut(&self, buf: &[u8], cx: &mut Context<'_>) -> Poll<Result<usize, zx::Status>> {
        let clear_closed = ready!(self.poll_write_task(cx))?;
        let res = self.handle.write(buf);
        if res == Err(zx::Status::SHOULD_WAIT) {
            self.need_write(cx, clear_closed)?;
            Poll::Pending
        } else {
            Poll::Ready(res)
        }
    }

    /// Polls for the next data on the socket, appending it to the end of |out| if it has arrived.
    /// Not very useful for a non-datagram socket as it will return all available data
    /// on the socket.
    pub fn poll_datagram(
        &self,
        cx: &mut Context<'_>,
        out: &mut Vec<u8>,
    ) -> Poll<Result<usize, zx::Status>> {
        let clear_closed = ready!(self.poll_read_task(cx))?;
        let avail = self.handle.outstanding_read_bytes()?;
        let len = out.len();
        out.resize(len + avail, 0);
        let (_, mut tail) = out.split_at_mut(len);
        match self.handle.read(&mut tail) {
            Err(zx::Status::SHOULD_WAIT) => {
                self.need_read(cx, clear_closed)?;
                Poll::Pending
            }
            Err(e) => Poll::Ready(Err(e)),
            Ok(bytes) => {
                if bytes == avail {
                    Poll::Ready(Ok(bytes))
                } else {
                    Poll::Ready(Err(zx::Status::BAD_STATE))
                }
            }
        }
    }

    /// Reads the next datagram that becomes available onto the end of |out|.  Note: Using this
    /// multiple times concurrently is an error and the first one will never complete.
    pub async fn read_datagram<'a>(&'a self, out: &'a mut Vec<u8>) -> Result<usize, zx::Status> {
        poll_fn(move |cx| self.poll_datagram(cx, out)).await
    }

    /// Use this socket as a stream of `Result<Vec<u8>, zx::Status>` datagrams.
    ///
    /// Note: multiple concurrent streams from the same socket are not supported.
    pub fn as_datagram_stream<'a>(&'a self) -> DatagramStream<&'a Self> {
        DatagramStream(self)
    }

    /// Convert this socket into a stream of `Result<Vec<u8>, zx::Status>` datagrams.
    pub fn into_datagram_stream(self) -> DatagramStream<Self> {
        DatagramStream(self)
    }
}

impl fmt::Debug for Socket {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.handle.fmt(f)
    }
}

impl AsyncRead for Socket {
    fn poll_read(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>> {
        self.read_nomut(buf, cx).map_err(Into::into)
    }
}

impl AsyncWrite for Socket {
    fn poll_write(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        self.write_nomut(buf, cx).map_err(Into::into)
    }

    fn poll_flush(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }
}

impl<'a> AsyncRead for &'a Socket {
    fn poll_read(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>> {
        self.read_nomut(buf, cx).map_err(Into::into)
    }
}

impl<'a> AsyncWrite for &'a Socket {
    fn poll_write(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        self.write_nomut(buf, cx).map_err(Into::into)
    }

    fn poll_flush(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }
}

/// A datagram stream from a `Socket`.
#[derive(Debug)]
pub struct DatagramStream<S>(pub S);

fn poll_datagram_as_stream(
    socket: &Socket,
    cx: &mut Context<'_>,
) -> Poll<Option<Result<Vec<u8>, zx::Status>>> {
    let mut res = Vec::<u8>::new();
    Poll::Ready(match ready!(socket.poll_datagram(cx, &mut res)) {
        Ok(_size) => Some(Ok(res)),
        Err(zx::Status::PEER_CLOSED) => None,
        Err(e) => Some(Err(e)),
    })
}

impl Stream for DatagramStream<Socket> {
    type Item = Result<Vec<u8>, zx::Status>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        poll_datagram_as_stream(&self.0, cx)
    }
}

impl Stream for DatagramStream<&Socket> {
    type Item = Result<Vec<u8>, zx::Status>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        poll_datagram_as_stream(self.0, cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        temp::{TempAsyncReadExt, TempAsyncWriteExt},
        Executor, Time, TimeoutExt, Timer,
    };
    use fuchsia_zircon::prelude::*;
    use futures::future::{try_join, FutureExt, TryFutureExt};
    use futures::stream::TryStreamExt;

    #[test]
    fn can_read_write() {
        let mut exec = Executor::new().unwrap();
        let bytes = &[0, 1, 2, 3];

        let (tx, rx) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let (tx, rx) = (Socket::from_socket(tx).unwrap(), Socket::from_socket(rx).unwrap());

        let receive_future = rx.read_to_end(vec![]).map_ok(|(_socket, buf)| {
            assert_eq!(&*buf, bytes);
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future.on_timeout(Time::after(300.millis()), || panic!("timeout"));

        // Sends a message after the timeout has passed
        let sender =
            Timer::new(Time::after(100.millis())).then(|()| tx.write_all(bytes)).map_ok(|_tx| ());

        let done = try_join(receiver, sender);
        exec.run_singlethreaded(done).unwrap();
    }

    #[test]
    fn can_read_datagram() {
        let mut exec = Executor::new().unwrap();

        let (one, two) = (&[0, 1], &[2, 3, 4, 5]);

        let (tx, rx) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let rx = Socket::from_socket(rx).unwrap();

        let mut out = vec![50];

        assert!(tx.write(one).is_ok());
        assert!(tx.write(two).is_ok());

        let size = exec.run_singlethreaded(rx.read_datagram(&mut out));

        assert!(size.is_ok());
        assert_eq!(one.len(), size.unwrap());

        assert_eq!([50, 0, 1], out.as_slice());

        let size = exec.run_singlethreaded(rx.read_datagram(&mut out));

        assert!(size.is_ok());
        assert_eq!(two.len(), size.unwrap());

        assert_eq!([50, 0, 1, 2, 3, 4, 5], out.as_slice());
    }

    #[test]
    fn stream_datagram() {
        let mut exec = Executor::new().unwrap();

        let (tx, rx) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let mut rx = Socket::from_socket(rx).unwrap().into_datagram_stream();

        let packets = 20;

        for size in 1..packets + 1 {
            let mut vec = Vec::<u8>::new();
            vec.resize(size, size as u8);
            assert!(tx.write(&vec).is_ok());
        }

        // Close the socket.
        drop(tx);

        let stream_read_fut = async move {
            let mut count = 0;
            while let Some(packet) = rx.try_next().await.expect("received error from stream") {
                count = count + 1;
                assert_eq!(packet.len(), count);
                assert!(packet.iter().all(|&x| x == count as u8));
            }
            assert_eq!(packets, count);
        };

        exec.run_singlethreaded(stream_read_fut);
    }
}
