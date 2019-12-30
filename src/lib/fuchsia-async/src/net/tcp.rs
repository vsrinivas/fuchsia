// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

use {
    crate::net::{set_nonblock, EventedFd},
    bytes::{Buf, BufMut},
    futures::{
        future::Future,
        io::{AsyncRead, AsyncWrite},
        ready,
        stream::Stream,
        task::{Context, Poll},
    },
    net2::{TcpBuilder, TcpStreamExt},
    std::{
        io::{self, Read, Write},
        marker::Unpin,
        net::{self, SocketAddr},
        ops::Deref,
        os::unix::io::AsRawFd,
        pin::Pin,
    },
};

/// An I/O object representing a TCP socket listening for incoming connections.
///
/// This object can be converted into a stream of incoming connections for
/// various forms of processing.
pub struct TcpListener(EventedFd<net::TcpListener>);

impl Unpin for TcpListener {}

impl Deref for TcpListener {
    type Target = EventedFd<net::TcpListener>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl TcpListener {
    /// Creates a new `TcpListener` bound to the provided socket.
    pub fn bind(addr: &SocketAddr) -> io::Result<TcpListener> {
        let sock = match *addr {
            SocketAddr::V4(..) => TcpBuilder::new_v4(),
            SocketAddr::V6(..) => TcpBuilder::new_v6(),
        }?;

        sock.reuse_address(true)?;
        sock.bind(addr)?;
        let listener = sock.listen(1024)?;
        TcpListener::from_std(listener)
    }

    /// Consumes this listener and returns a `Future` that resolves to an
    /// `io::Result<(TcpListener, TcpStream, SocketAddr)>`.
    pub fn accept(self) -> Acceptor {
        Acceptor(Some(self))
    }

    /// Consumes this listener and returns a `Stream` that resolves to elements
    /// of type `io::Result<(TcpStream, SocketAddr)>`.
    pub fn accept_stream(self) -> AcceptStream {
        AcceptStream(self)
    }

    /// Poll on `accept`ing a new `TcpStream` from this listener.
    /// This function is mainly intended for usage in manual `Future` or `Stream`
    /// implementations.
    pub fn async_accept(
        &mut self,
        cx: &mut Context<'_>,
    ) -> Poll<io::Result<(TcpStream, SocketAddr)>> {
        ready!(EventedFd::poll_readable(&self.0, cx))?;
        match self.0.as_ref().accept() {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_read(cx);
                    Poll::Pending
                } else {
                    Poll::Ready(Err(e))
                }
            }
            Ok((sock, addr)) => Poll::Ready(Ok((TcpStream::from_std(sock)?, addr))),
        }
    }

    /// Creates a new instance of `fuchsia_async::net::TcpListener` from an
    /// `std::net::TcpListener`.
    pub fn from_std(listener: net::TcpListener) -> io::Result<TcpListener> {
        set_nonblock(listener.as_raw_fd())?;
        unsafe { Ok(TcpListener(EventedFd::new(listener)?)) }
    }

    /// Returns a reference to the underlying `std::net::TcpListener`.
    pub fn std(&self) -> &net::TcpListener {
        self.as_ref()
    }

    /// Returns the local socket address of the listener.
    pub fn local_addr(&self) -> io::Result<net::SocketAddr> {
        self.std().local_addr()
    }
}

/// A future which resolves to an `io::Result<(TcpListener, TcpStream, SocketAddr)>`.
pub struct Acceptor(Option<TcpListener>);

impl Future for Acceptor {
    type Output = io::Result<(TcpListener, TcpStream, SocketAddr)>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let (stream, addr);
        {
            let listener = self.0.as_mut().expect("polled an Acceptor after completion");
            let (s, a) = ready!(listener.async_accept(cx))?;
            stream = s;
            addr = a;
        }
        let listener = self.0.take().unwrap();
        Poll::Ready(Ok((listener, stream, addr)))
    }
}

/// A stream which resolves to an `io::Result<(TcpStream, SocketAddr)>`.
pub struct AcceptStream(TcpListener);

impl Stream for AcceptStream {
    type Item = io::Result<(TcpStream, SocketAddr)>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let (stream, addr) = ready!(self.0.async_accept(cx)?);
        Poll::Ready(Some(Ok((stream, addr))))
    }
}

enum ConnectState {
    // The stream has not yet connected to an address.
    New,
    // `connect` has been called on the stream but it has not yet completed.
    Connecting,
    // `connect` has succeeded. Subsequent attempts to connect will fail.
    // state.
    Connected,
}

/// A single TCP connection.
///
/// This type and references to it implement the `AsyncRead` and `AsyncWrite`
/// traits. For more on using this type, see the `AsyncReadExt` and `AsyncWriteExt`
/// traits.
pub struct TcpStream {
    state: ConnectState,
    stream: EventedFd<net::TcpStream>,
}

impl Deref for TcpStream {
    type Target = EventedFd<net::TcpStream>;

    fn deref(&self) -> &Self::Target {
        &self.stream
    }
}

impl TcpStream {
    /// Creates a new `TcpStream` connected to a specific socket address.
    /// This function returns a future which resolves to an `io::Result<TcpStream>`.
    pub fn connect(addr: SocketAddr) -> io::Result<TcpConnector> {
        let sock = match addr {
            SocketAddr::V4(..) => TcpBuilder::new_v4(),
            SocketAddr::V6(..) => TcpBuilder::new_v6(),
        }?;

        let stream = sock.to_tcp_stream()?;
        set_nonblock(stream.as_raw_fd())?;
        // This is safe because the file descriptor for stream will live as long as the TcpStream.
        let stream = unsafe { EventedFd::new(stream)? };
        let stream = TcpStream { state: ConnectState::New, stream };

        Ok(TcpConnector { addr, stream: Some(stream) })
    }

    /// Poll on successful connection of this `TcpStream`.
    /// This function is mainly intended for usage in manual `Future` or `Stream`
    /// implementations.
    pub fn async_connect(
        &mut self,
        addr: &SocketAddr,
        cx: &mut Context<'_>,
    ) -> Poll<io::Result<()>> {
        match self.state {
            ConnectState::New => match self.stream.as_ref().connect(addr) {
                Err(e) => {
                    if e.raw_os_error() == Some(libc::EINPROGRESS) {
                        self.state = ConnectState::Connecting;
                        self.stream.need_write(cx);
                        Poll::Pending
                    } else {
                        Poll::Ready(Err(e))
                    }
                }
                Ok(()) => {
                    self.state = ConnectState::Connected;
                    Poll::Ready(Ok(()))
                }
            },
            ConnectState::Connecting => self.stream.poll_writable(cx).map(|res| match res {
                Err(e) => Err(e.into()),
                Ok(()) => match self.stream.as_ref().take_error() {
                    Err(err) | Ok(Some(err)) => Err(err),
                    Ok(None) => {
                        self.state = ConnectState::Connected;
                        Ok(())
                    }
                },
            }),
            ConnectState::Connected => {
                Poll::Ready(Err(io::Error::from_raw_os_error(libc::EISCONN)))
            }
        }
    }

    /// Polls on attempting to read data from this stream into a `BufMut`.
    pub fn read_buf<B: BufMut>(
        &self,
        buf: &mut B,
        cx: &mut Context<'_>,
    ) -> Poll<io::Result<usize>> {
        match (&self.stream).as_ref().read(unsafe { buf.bytes_mut() }) {
            Ok(n) => {
                unsafe {
                    buf.advance_mut(n);
                }
                Poll::Ready(Ok(n))
            }
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                self.stream.need_read(cx);
                Poll::Pending
            }
            Err(e) => Poll::Ready(Err(e)),
        }
    }

    /// Polls on attempting to write data into this stream from a `Buf`.
    pub fn write_buf<B: Buf>(&self, buf: &mut B, cx: &mut Context<'_>) -> Poll<io::Result<usize>> {
        match (&self.stream).as_ref().write(buf.bytes()) {
            Ok(n) => {
                buf.advance(n);
                Poll::Ready(Ok(n))
            }
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                self.stream.need_write(cx);
                Poll::Pending
            }
            Err(e) => Poll::Ready(Err(e)),
        }
    }

    /// Creates a new `fuchsia_async::net::TcpStream` from a `std::net::TcpStream`.
    fn from_std(stream: net::TcpStream) -> io::Result<TcpStream> {
        set_nonblock(stream.as_raw_fd())?;

        // This is safe because the file descriptor for stream will live as long as the TcpStream.
        let stream = unsafe { EventedFd::new(stream)? };

        Ok(TcpStream { state: ConnectState::Connected, stream })
    }

    /// Returns a reference to the underlying `std::net::TcpStream`
    pub fn std(&self) -> &net::TcpStream {
        self.as_ref()
    }
}

impl AsyncRead for TcpStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.stream).poll_read(cx, buf)
    }

    // TODO: override poll_vectored_read and call readv on the underlying stream
}

impl AsyncWrite for TcpStream {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.stream).poll_write(cx, buf)
    }

    fn poll_flush(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<io::Result<()>> {
        Poll::Ready(Ok(()))
    }

    // TODO: override poll_vectored_write and call writev on the underlying stream
}

/// A future which resolves to a connected `TcpStream`.
pub struct TcpConnector {
    addr: SocketAddr,
    stream: Option<TcpStream>,
}

impl Future for TcpConnector {
    type Output = io::Result<TcpStream>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        {
            let stream = this.stream.as_mut().expect("polled a TcpConnector after completion");
            ready!(stream.async_connect(&this.addr, cx))?;
        }
        let stream = this.stream.take().unwrap();
        Poll::Ready(Ok(stream))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{TcpListener, TcpStream},
        crate::Executor,
        futures::{
            io::{AsyncReadExt, AsyncWriteExt},
            stream::StreamExt,
        },
        net2::TcpBuilder,
        std::{
            io::{Error, ErrorKind},
            net::{self, Ipv4Addr, SocketAddr},
        },
    };

    #[test]
    fn choose_listen_port() {
        let _exec = Executor::new().expect("could not create executor");
        let addr_request = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);
        let listener = TcpListener::bind(&addr_request).expect("could not create listener");
        let actual_addr = listener.local_addr().expect("local_addr query to succeed");
        assert_eq!(actual_addr.ip(), addr_request.ip());
        assert_ne!(actual_addr.port(), 0);
    }

    #[test]
    fn choose_listen_port_from_std() {
        let _exec = Executor::new().expect("could not create executor");
        let addr_request = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);
        let inner = net::TcpListener::bind(&addr_request).expect("could not create inner listener");
        let listener = TcpListener::from_std(inner).expect("could not create listener");
        let actual_addr = listener.local_addr().expect("local_addr query to succeed");
        assert_eq!(actual_addr.ip(), addr_request.ip());
        assert_ne!(actual_addr.port(), 0);
    }

    #[test]
    fn connect_to_nonlistening_endpoint() {
        let mut exec = Executor::new().expect("could not create executor");

        // bind to a port to find an unused one, but don't start listening.
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);
        let socket = TcpBuilder::new_v4().unwrap();
        let addr = socket
            .bind(addr)
            .expect("could not bind")
            .local_addr()
            .expect("local_addr query to succeed");

        // connecting to the nonlistening port should fail.
        let connector = TcpStream::connect(addr).expect("could not create client");
        let fut = async move {
            let res = connector.await;
            assert!(res.is_err());
            Ok::<(), Error>(())
        };

        exec.run_singlethreaded(fut).expect("failed to run tcp socket test");
    }

    #[test]
    fn send_recv() {
        let mut exec = Executor::new().expect("could not create executor");

        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);
        let listener = TcpListener::bind(&addr).expect("could not create listener");
        let addr = listener.local_addr().expect("local_addr query to succeed");
        let mut listener = listener.accept_stream();

        let query = b"ping";
        let response = b"pong";

        let server = async move {
            let (mut socket, _clientaddr) =
                listener.next().await.expect("stream to not be done").expect("client to connect");
            drop(listener);

            let mut buf = [0u8; 20];
            let n = socket.read(&mut buf[..]).await.expect("server read to succeed");
            assert_eq!(query, &buf[..n]);

            socket.write_all(&response[..]).await.expect("server write to succeed");

            let err = socket.read_exact(&mut buf[..]).await.unwrap_err();
            assert_eq!(err.kind(), ErrorKind::UnexpectedEof);
        };

        let client = async move {
            let connector = TcpStream::connect(addr).expect("could not create client");
            let mut socket = connector.await.expect("client to connect to server");

            socket.write_all(&query[..]).await.expect("client write to succeed");

            let mut buf = [0u8; 20];
            let n = socket.read(&mut buf[..]).await.expect("client read to succeed");
            assert_eq!(response, &buf[..n]);
        };

        exec.run_singlethreaded(futures::future::join(server, client));
    }

    #[test]
    fn send_recv_large() {
        let mut exec = Executor::new().expect("could not create executor");
        let addr = "127.0.0.1:0".parse().unwrap();

        const BUF_SIZE: usize = 10 * 1024;
        const WRITES: usize = 1024;
        const LENGTH: usize = WRITES * BUF_SIZE;

        let listener = TcpListener::bind(&addr).expect("could not create listener");
        let addr = listener.local_addr().expect("query local_addr");
        let mut listener = listener.accept_stream();

        let server = async move {
            let (mut socket, _clientaddr) =
                listener.next().await.expect("stream to not be done").expect("client to connect");
            drop(listener);

            let buf = [0u8; BUF_SIZE];
            for _ in 0usize..WRITES {
                socket.write_all(&buf[..]).await.expect("server write to succeed");
            }
        };

        let client = async move {
            let connector = TcpStream::connect(addr).expect("could not create client");
            let mut socket = connector.await.expect("client to connect to server");

            let zeroes = Box::new([0u8; BUF_SIZE]);
            let mut read = 0;
            while read < LENGTH {
                let mut buf = Box::new([1u8; BUF_SIZE]);
                let n = socket.read(&mut buf[..]).await.expect("client read to succeed");
                assert_eq!(&buf[0..n], &zeroes[0..n]);
                read += n;
            }
        };

        exec.run_singlethreaded(futures::future::join(server, client));
    }
}
