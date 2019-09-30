//! Asynchronous TLS/SSL streams for Tokio using [Rustls](https://github.com/ctz/rustls).

pub extern crate rustls;
pub extern crate webpki;

extern crate bytes;
extern crate futures;
extern crate iovec;
extern crate tokio_io;

pub mod client;
mod common;
pub mod server;

use common::Stream;
use futures::{Async, Future, Poll};
use rustls::{ClientConfig, ClientSession, ServerConfig, ServerSession, Session};
use std::sync::Arc;
use std::{io, mem};
use tokio_io::{try_nb, AsyncRead, AsyncWrite};
use webpki::DNSNameRef;

#[derive(Debug, Copy, Clone)]
pub enum TlsState {
    #[cfg(feature = "early-data")]
    EarlyData,
    Stream,
    ReadShutdown,
    WriteShutdown,
    FullyShutdown,
}

impl TlsState {
    pub(crate) fn shutdown_read(&mut self) {
        match *self {
            TlsState::WriteShutdown | TlsState::FullyShutdown => *self = TlsState::FullyShutdown,
            _ => *self = TlsState::ReadShutdown,
        }
    }

    pub(crate) fn shutdown_write(&mut self) {
        match *self {
            TlsState::ReadShutdown | TlsState::FullyShutdown => *self = TlsState::FullyShutdown,
            _ => *self = TlsState::WriteShutdown,
        }
    }

    pub(crate) fn writeable(&self) -> bool {
        match *self {
            TlsState::WriteShutdown | TlsState::FullyShutdown => false,
            _ => true,
        }
    }

    pub(crate) fn readable(self) -> bool {
        match self {
            TlsState::ReadShutdown | TlsState::FullyShutdown => false,
            _ => true,
        }
    }
}

/// A wrapper around a `rustls::ClientConfig`, providing an async `connect` method.
#[derive(Clone)]
pub struct TlsConnector {
    inner: Arc<ClientConfig>,
    #[cfg(feature = "early-data")]
    early_data: bool,
}

/// A wrapper around a `rustls::ServerConfig`, providing an async `accept` method.
#[derive(Clone)]
pub struct TlsAcceptor {
    inner: Arc<ServerConfig>,
}

impl From<Arc<ClientConfig>> for TlsConnector {
    fn from(inner: Arc<ClientConfig>) -> TlsConnector {
        TlsConnector {
            inner,
            #[cfg(feature = "early-data")]
            early_data: false,
        }
    }
}

impl From<Arc<ServerConfig>> for TlsAcceptor {
    fn from(inner: Arc<ServerConfig>) -> TlsAcceptor {
        TlsAcceptor { inner }
    }
}

impl TlsConnector {
    /// Enable 0-RTT.
    ///
    /// Note that you want to use 0-RTT.
    /// You must set `enable_early_data` to `true` in `ClientConfig`.
    #[cfg(feature = "early-data")]
    pub fn early_data(mut self, flag: bool) -> TlsConnector {
        self.early_data = flag;
        self
    }

    pub fn connect<IO>(&self, domain: DNSNameRef, stream: IO) -> Connect<IO>
    where
        IO: AsyncRead + AsyncWrite,
    {
        self.connect_with(domain, stream, |_| ())
    }

    #[inline]
    pub fn connect_with<IO, F>(&self, domain: DNSNameRef, stream: IO, f: F) -> Connect<IO>
    where
        IO: AsyncRead + AsyncWrite,
        F: FnOnce(&mut ClientSession),
    {
        let mut session = ClientSession::new(&self.inner, domain);
        f(&mut session);

        #[cfg(not(feature = "early-data"))]
        {
            Connect(client::MidHandshake::Handshaking(client::TlsStream {
                session,
                io: stream,
                state: TlsState::Stream,
            }))
        }

        #[cfg(feature = "early-data")]
        {
            Connect(if self.early_data {
                client::MidHandshake::EarlyData(client::TlsStream {
                    session,
                    io: stream,
                    state: TlsState::EarlyData,
                    early_data: (0, Vec::new()),
                })
            } else {
                client::MidHandshake::Handshaking(client::TlsStream {
                    session,
                    io: stream,
                    state: TlsState::Stream,
                    early_data: (0, Vec::new()),
                })
            })
        }
    }
}

impl TlsAcceptor {
    pub fn accept<IO>(&self, stream: IO) -> Accept<IO>
    where
        IO: AsyncRead + AsyncWrite,
    {
        self.accept_with(stream, |_| ())
    }

    #[inline]
    pub fn accept_with<IO, F>(&self, stream: IO, f: F) -> Accept<IO>
    where
        IO: AsyncRead + AsyncWrite,
        F: FnOnce(&mut ServerSession),
    {
        let mut session = ServerSession::new(&self.inner);
        f(&mut session);

        Accept(server::MidHandshake::Handshaking(server::TlsStream {
            session,
            io: stream,
            state: TlsState::Stream,
        }))
    }
}

/// Future returned from `ClientConfigExt::connect_async` which will resolve
/// once the connection handshake has finished.
pub struct Connect<IO>(client::MidHandshake<IO>);

/// Future returned from `ServerConfigExt::accept_async` which will resolve
/// once the accept handshake has finished.
pub struct Accept<IO>(server::MidHandshake<IO>);

impl<IO> Connect<IO> {
    pub fn take_inner(&mut self) -> Option<IO> {
        match mem::replace(&mut self.0, client::MidHandshake::End) {
            client::MidHandshake::Handshaking(client::TlsStream { io, .. }) => Some(io),
            _ => None
        }
    }
}

impl<IO> Accept<IO> {
    pub fn take_inner(&mut self) -> Option<IO> {
        match mem::replace(&mut self.0, server::MidHandshake::End) {
            server::MidHandshake::Handshaking(server::TlsStream { io, .. }) => Some(io),
            _ => None
        }
    }
}

impl<IO: AsyncRead + AsyncWrite> Future for Connect<IO> {
    type Item = client::TlsStream<IO>;
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        self.0.poll()
    }
}

impl<IO: AsyncRead + AsyncWrite> Future for Accept<IO> {
    type Item = server::TlsStream<IO>;
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        self.0.poll()
    }
}

/// Unified TLS stream type
///
/// This abstracts over the inner `client::TlsStream` and `server::TlsStream`, so you can use
/// a single type to keep both client- and server-initiated TLS-encrypted connections.
pub enum TlsStream<T> {
    Client(client::TlsStream<T>),
    Server(server::TlsStream<T>),
}

impl<T> TlsStream<T> {
    pub fn get_ref(&self) -> (&T, &dyn Session) {
        use TlsStream::*;
        match self {
            Client(io) => {
                let (io, session) = io.get_ref();
                (io, &*session)
            }
            Server(io) => {
                let (io, session) = io.get_ref();
                (io, &*session)
            }
        }
    }

    pub fn get_mut(&mut self) -> (&mut T, &mut dyn Session) {
        use TlsStream::*;
        match self {
            Client(io) => {
                let (io, session) = io.get_mut();
                (io, &mut *session)
            }
            Server(io) => {
                let (io, session) = io.get_mut();
                (io, &mut *session)
            }
        }
    }
}

impl<T> From<client::TlsStream<T>> for TlsStream<T> {
    fn from(s: client::TlsStream<T>) -> Self {
        Self::Client(s)
    }
}

impl<T> From<server::TlsStream<T>> for TlsStream<T> {
    fn from(s: server::TlsStream<T>) -> Self {
        Self::Server(s)
    }
}

impl<T> io::Read for TlsStream<T>
where
    T: AsyncRead + AsyncWrite + io::Read,
{
    fn read(&mut self, buf: &mut [u8]) -> Result<usize, io::Error> {
        use TlsStream::*;
        match self {
            Client(io) => io.read(buf),
            Server(io) => io.read(buf),
        }
    }
}

impl<T> io::Write for TlsStream<T>
where
    T: AsyncRead + AsyncWrite + io::Write,
{
    fn write(&mut self, buf: &[u8]) -> Result<usize, io::Error> {
        use TlsStream::*;
        match self {
            Client(io) => io.write(buf),
            Server(io) => io.write(buf),
        }
    }

    fn flush(&mut self) -> Result<(), io::Error> {
        use TlsStream::*;
        match self {
            Client(io) => io.flush(),
            Server(io) => io.flush(),
        }
    }
}

impl<T> AsyncRead for TlsStream<T> where T: AsyncRead + AsyncWrite {}

impl<T> AsyncWrite for TlsStream<T>
where
    T: AsyncRead + AsyncWrite,
{
    fn shutdown(&mut self) -> Poll<(), io::Error> {
        use TlsStream::*;
        match self {
            Client(io) => io.shutdown(),
            Server(io) => io.shutdown(),
        }
    }
}

#[cfg(feature = "early-data")]
#[cfg(test)]
mod test_0rtt;
