use super::*;
use tokio_io::{ AsyncRead, AsyncWrite };
use futures::{Async, Future, Poll};
use common::Stream;


macro_rules! try_async {
    ( $e:expr ) => {
        match $e {
            Ok(n) => n,
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock =>
                return Ok(Async::NotReady),
            Err(e) => return Err(e)
        }
    }
}

impl<IO: AsyncRead + AsyncWrite> Future for Connect<IO> {
    type Item = TlsStream<IO, ClientSession>;
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        self.0.poll()
    }
}

impl<IO: AsyncRead + AsyncWrite> Future for Accept<IO> {
    type Item = TlsStream<IO, ServerSession>;
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        self.0.poll()
    }
}

impl<IO, S> Future for MidHandshake<IO, S>
where
    IO: io::Read + io::Write,
    S: Session
{
    type Item = TlsStream<IO, S>;
    type Error = io::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        {
            let stream = self.inner.as_mut().unwrap();
            let (io, session) = stream.get_mut();
            let mut stream = Stream::new(session, io);

            if stream.session.is_handshaking() {
                try_async!(stream.complete_io());
            }

            if stream.session.wants_write() {
                try_async!(stream.complete_io());
            }
        }

        Ok(Async::Ready(self.inner.take().unwrap()))
    }
}

impl<IO, S> AsyncRead for TlsStream<IO, S>
    where
        IO: AsyncRead + AsyncWrite,
        S: Session
{
    unsafe fn prepare_uninitialized_buffer(&self, _: &mut [u8]) -> bool {
        false
    }
}

impl<IO, S> AsyncWrite for TlsStream<IO, S>
    where
        IO: AsyncRead + AsyncWrite,
        S: Session
{
    fn shutdown(&mut self) -> Poll<(), io::Error> {
        if !self.is_shutdown {
            self.session.send_close_notify();
            self.is_shutdown = true;
        }

        {
            let mut stream = Stream::new(&mut self.session, &mut self.io);
            try_async!(stream.complete_io());
        }
        self.io.shutdown()
    }
}
