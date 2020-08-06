// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

use {
    crate::net::EventedFd,
    futures::{
        future::Future,
        io::{AsyncRead, AsyncWrite},
        ready,
        stream::Stream,
        task::{Context, Poll},
    },
    std::{
        io,
        marker::Unpin,
        net::{self, SocketAddr},
        ops::Deref,
        os::unix::io::FromRawFd as _,
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
        let domain = match *addr {
            SocketAddr::V4(..) => socket2::Domain::ipv4(),
            SocketAddr::V6(..) => socket2::Domain::ipv6(),
        };
        let socket =
            socket2::Socket::new(domain, socket2::Type::stream(), Some(socket2::Protocol::tcp()))?;
        let () = socket.set_reuse_address(true)?;
        let addr = (*addr).into();
        let () = socket.bind(&addr)?;
        let () = socket.listen(1024)?;
        TcpListener::from_std(socket.into_tcp_listener())
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
        let listener: socket2::Socket = listener.into();
        let () = listener.set_nonblocking(true)?;
        let listener = listener.into_tcp_listener();
        let listener = unsafe { EventedFd::new(listener)? };
        Ok(TcpListener(listener))
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

/// A single TCP connection.
///
/// This type and references to it implement the `AsyncRead` and `AsyncWrite`
/// traits. For more on using this type, see the `AsyncReadExt` and `AsyncWriteExt`
/// traits.
pub struct TcpStream {
    stream: EventedFd<net::TcpStream>,
}

impl Deref for TcpStream {
    type Target = EventedFd<net::TcpStream>;

    fn deref(&self) -> &Self::Target {
        &self.stream
    }
}

impl TcpStream {
    /// Creates a new `TcpStream` connected to a specific socket address from an existing socket
    /// descriptor.
    /// This function returns a future which resolves to an `io::Result<TcpStream>`.
    pub fn connect_from_raw(
        socket: impl std::os::unix::io::IntoRawFd,
        addr: SocketAddr,
    ) -> io::Result<TcpConnector> {
        // This is safe because `into_raw_fd()` consumes ownership of the socket, so we are
        // guaranteed that the returned value is not shared among more than one owner at this
        // point.
        let socket = unsafe { socket2::Socket::from_raw_fd(socket.into_raw_fd()) };
        Self::from_socket2(socket, addr)
    }

    /// Creates a new `TcpStream` connected to a specific socket address.
    ///
    /// This function returns a future which resolves to an `io::Result<TcpStream>`.
    pub fn connect(addr: SocketAddr) -> io::Result<TcpConnector> {
        let domain = match addr {
            SocketAddr::V4(..) => socket2::Domain::ipv4(),
            SocketAddr::V6(..) => socket2::Domain::ipv6(),
        };
        let socket =
            socket2::Socket::new(domain, socket2::Type::stream(), Some(socket2::Protocol::tcp()))?;
        Self::from_socket2(socket, addr)
    }

    // This function is intentionally kept private to avoid socket2 appearing in the public API.
    fn from_socket2(socket: socket2::Socket, addr: SocketAddr) -> io::Result<TcpConnector> {
        let () = socket.set_nonblocking(true)?;
        let addr = addr.into();
        let () = match socket.connect(&addr) {
            Err(e) if e.raw_os_error() == Some(libc::EINPROGRESS) => Ok(()),
            res => res,
        }?;
        let stream = socket.into_tcp_stream();
        // This is safe because the file descriptor for stream will live as long as the TcpStream.
        let stream = unsafe { EventedFd::new(stream)? };
        let stream = Some(TcpStream { stream });

        Ok(TcpConnector { need_write: true, stream })
    }

    /// Creates a new `fuchsia_async::net::TcpStream` from a `std::net::TcpStream`.
    fn from_std(stream: net::TcpStream) -> io::Result<TcpStream> {
        let stream: socket2::Socket = stream.into();
        let () = stream.set_nonblocking(true)?;
        let stream = stream.into_tcp_stream();
        // This is safe because the file descriptor for stream will live as long as the TcpStream.
        let stream = unsafe { EventedFd::new(stream)? };
        Ok(TcpStream { stream })
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
    // The stream needs to have `need_write` called on it to defeat the optimization in
    // EventedFd::new which assumes that the operand is immediately readable and writable.
    need_write: bool,
    stream: Option<TcpStream>,
}

impl Future for TcpConnector {
    type Output = io::Result<TcpStream>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        {
            let stream = this.stream.as_mut().expect("polled a TcpConnector after completion");
            if this.need_write {
                this.need_write = false;
                stream.need_write(cx);
                return Poll::Pending;
            }
            let () = ready!(stream.poll_writable(cx)?);
            let () = match stream.as_ref().take_error() {
                Ok(None) => Ok(()),
                Ok(Some(err)) | Err(err) => Err(err),
            }?;
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
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0).into();
        let socket = socket2::Socket::new(
            socket2::Domain::ipv4(),
            socket2::Type::stream(),
            Some(socket2::Protocol::tcp()),
        )
        .expect("could not create socket");
        let () = socket.bind(&addr).expect("could not bind");
        let addr = socket.local_addr().expect("local addr query to succeed");
        let addr = addr.as_std().expect("local addr to be ipv4 or ipv6");

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
