//! Asynchronous TLS/SSL streams for Tokio using [Rustls](https://github.com/ctz/rustls).

#![cfg_attr(feature = "nightly", feature(specialization, read_initializer))]

pub extern crate rustls;
pub extern crate webpki;

#[cfg(feature = "tokio-support")]
extern crate futures;
#[cfg(feature = "tokio-support")]
extern crate tokio_io;
#[cfg(feature = "nightly")]
#[cfg(feature = "tokio-support")]
extern crate bytes;
#[cfg(feature = "nightly")]
#[cfg(feature = "tokio-support")]
extern crate iovec;


mod common;
#[cfg(feature = "tokio-support")] mod tokio_impl;

use std::io;
use std::sync::Arc;
#[cfg(feature = "nightly")]
use std::io::Initializer;
use webpki::DNSNameRef;
use rustls::{
    Session, ClientSession, ServerSession,
    ClientConfig, ServerConfig,
};
use common::Stream;


#[derive(Clone)]
pub struct TlsConnector {
    inner: Arc<ClientConfig>
}

#[derive(Clone)]
pub struct TlsAcceptor {
    inner: Arc<ServerConfig>
}

impl From<Arc<ClientConfig>> for TlsConnector {
    fn from(inner: Arc<ClientConfig>) -> TlsConnector {
        TlsConnector { inner }
    }
}

impl From<Arc<ServerConfig>> for TlsAcceptor {
    fn from(inner: Arc<ServerConfig>) -> TlsAcceptor {
        TlsAcceptor { inner }
    }
}

impl TlsConnector {
    pub fn connect<IO>(&self, domain: DNSNameRef, stream: IO) -> Connect<IO>
        where IO: io::Read + io::Write
    {
        Self::connect_with_session(stream, ClientSession::new(&self.inner, domain))
    }

    #[inline]
    pub fn connect_with_session<IO>(stream: IO, session: ClientSession)
        -> Connect<IO>
        where IO: io::Read + io::Write
    {
        Connect(MidHandshake {
            inner: Some(TlsStream { session, io: stream, is_shutdown: false, eof: false })
        })
    }
}

impl TlsAcceptor {
    pub fn accept<IO>(&self, stream: IO) -> Accept<IO>
        where IO: io::Read + io::Write,
    {
        Self::accept_with_session(stream, ServerSession::new(&self.inner))
    }

    #[inline]
    pub fn accept_with_session<IO>(stream: IO, session: ServerSession) -> Accept<IO>
        where IO: io::Read + io::Write
    {
        Accept(MidHandshake {
            inner: Some(TlsStream { session, io: stream, is_shutdown: false, eof: false })
        })
    }
}


/// Future returned from `ClientConfigExt::connect_async` which will resolve
/// once the connection handshake has finished.
pub struct Connect<IO>(MidHandshake<IO, ClientSession>);

/// Future returned from `ServerConfigExt::accept_async` which will resolve
/// once the accept handshake has finished.
pub struct Accept<IO>(MidHandshake<IO, ServerSession>);


struct MidHandshake<IO, S> {
    inner: Option<TlsStream<IO, S>>
}


/// A wrapper around an underlying raw stream which implements the TLS or SSL
/// protocol.
#[derive(Debug)]
pub struct TlsStream<IO, S> {
    is_shutdown: bool,
    eof: bool,
    io: IO,
    session: S
}

impl<IO, S> TlsStream<IO, S> {
    #[inline]
    pub fn get_ref(&self) -> (&IO, &S) {
        (&self.io, &self.session)
    }

    #[inline]
    pub fn get_mut(&mut self) -> (&mut IO, &mut S) {
        (&mut self.io, &mut self.session)
    }

    #[inline]
    pub fn into_inner(self) -> (IO, S) {
        (self.io, self.session)
    }
}

impl<IO, S: Session> From<(IO, S)> for TlsStream<IO, S> {
    #[inline]
    fn from((io, session): (IO, S)) -> TlsStream<IO, S> {
        assert!(!session.is_handshaking());

        TlsStream {
            is_shutdown: false,
            eof: false,
            io, session
        }
    }
}

impl<IO, S> io::Read for TlsStream<IO, S>
    where IO: io::Read + io::Write, S: Session
{
    #[cfg(feature = "nightly")]
    unsafe fn initializer(&self) -> Initializer {
        Initializer::nop()
    }

    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if self.eof {
            return Ok(0);
        }

        match Stream::new(&mut self.session, &mut self.io).read(buf) {
            Ok(0) => { self.eof = true; Ok(0) },
            Ok(n) => Ok(n),
            Err(ref e) if e.kind() == io::ErrorKind::ConnectionAborted => {
                self.eof = true;
                self.is_shutdown = true;
                self.session.send_close_notify();
                Ok(0)
            },
            Err(e) => Err(e)
        }
    }
}

impl<IO, S> io::Write for TlsStream<IO, S>
    where IO: io::Read + io::Write, S: Session
{
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        Stream::new(&mut self.session, &mut self.io).write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        Stream::new(&mut self.session, &mut self.io).flush()?;
        self.io.flush()
    }
}
