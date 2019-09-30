use super::*;
use rustls::Session;

/// A wrapper around an underlying raw stream which implements the TLS or SSL
/// protocol.
#[derive(Debug)]
pub struct TlsStream<IO> {
    pub(crate) io: IO,
    pub(crate) session: ServerSession,
    pub(crate) state: TlsState,
}

pub(crate) enum MidHandshake<IO> {
    Handshaking(TlsStream<IO>),
    End,
}

impl<IO> TlsStream<IO> {
    #[inline]
    pub fn get_ref(&self) -> (&IO, &ServerSession) {
        (&self.io, &self.session)
    }

    #[inline]
    pub fn get_mut(&mut self) -> (&mut IO, &mut ServerSession) {
        (&mut self.io, &mut self.session)
    }

    #[inline]
    pub fn into_inner(self) -> (IO, ServerSession) {
        (self.io, self.session)
    }
}

impl<IO> Future for MidHandshake<IO>
where
    IO: AsyncRead + AsyncWrite,
{
    type Item = TlsStream<IO>;
    type Error = io::Error;

    #[inline]
    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        if let MidHandshake::Handshaking(stream) = self {
            let state = stream.state;
            let (io, session) = stream.get_mut();
            let mut stream = Stream::new(io, session).set_eof(!state.readable());

            if stream.session.is_handshaking() {
                try_nb!(stream.complete_io());
            }

            if stream.session.wants_write() {
                try_nb!(stream.complete_io());
            }
        }

        match mem::replace(self, MidHandshake::End) {
            MidHandshake::Handshaking(stream) => Ok(Async::Ready(stream)),
            MidHandshake::End => panic!(),
        }
    }
}

impl<IO> io::Read for TlsStream<IO>
where
    IO: AsyncRead + AsyncWrite,
{
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let mut stream = Stream::new(&mut self.io, &mut self.session).set_eof(!self.state.readable());

        match self.state {
            TlsState::Stream | TlsState::WriteShutdown => match stream.read(buf) {
                Ok(0) => {
                    self.state.shutdown_read();
                    Ok(0)
                }
                Ok(n) => Ok(n),
                Err(ref e) if e.kind() == io::ErrorKind::ConnectionAborted => {
                    self.state.shutdown_read();
                    if self.state.writeable() {
                        stream.session.send_close_notify();
                        self.state.shutdown_write();
                    }
                    Ok(0)
                }
                Err(e) => Err(e),
            },
            TlsState::ReadShutdown | TlsState::FullyShutdown => Ok(0),
            #[cfg(feature = "early-data")]
            s => unreachable!("server TLS can not hit this state: {:?}", s),
        }
    }
}

impl<IO> io::Write for TlsStream<IO>
where
    IO: AsyncRead + AsyncWrite,
{
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        Stream::new(&mut self.io, &mut self.session)
            .set_eof(!self.state.readable())
            .write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        Stream::new(&mut self.io, &mut self.session)
            .set_eof(!self.state.readable())
            .flush()?;
        self.io.flush()
    }
}

impl<IO> AsyncRead for TlsStream<IO>
where
    IO: AsyncRead + AsyncWrite,
{
    unsafe fn prepare_uninitialized_buffer(&self, _: &mut [u8]) -> bool {
        false
    }
}

impl<IO> AsyncWrite for TlsStream<IO>
where
    IO: AsyncRead + AsyncWrite,
{
    fn shutdown(&mut self) -> Poll<(), io::Error> {
        if self.state.writeable() {
            self.session.send_close_notify();
            self.state.shutdown_write();
        }

        let mut stream = Stream::new(&mut self.io, &mut self.session).set_eof(!self.state.readable());
        try_nb!(stream.complete_io());
        stream.io.shutdown()
    }
}
