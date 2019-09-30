use super::*;
use rustls::Session;
use std::io::Write;

/// A wrapper around an underlying raw stream which implements the TLS or SSL
/// protocol.
#[derive(Debug)]
pub struct TlsStream<IO> {
    pub(crate) io: IO,
    pub(crate) session: ClientSession,
    pub(crate) state: TlsState,

    #[cfg(feature = "early-data")]
    pub(crate) early_data: (usize, Vec<u8>),
}

pub(crate) enum MidHandshake<IO> {
    Handshaking(TlsStream<IO>),
    #[cfg(feature = "early-data")]
    EarlyData(TlsStream<IO>),
    End,
}

impl<IO> TlsStream<IO> {
    #[inline]
    pub fn get_ref(&self) -> (&IO, &ClientSession) {
        (&self.io, &self.session)
    }

    #[inline]
    pub fn get_mut(&mut self) -> (&mut IO, &mut ClientSession) {
        (&mut self.io, &mut self.session)
    }

    #[inline]
    pub fn into_inner(self) -> (IO, ClientSession) {
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
            #[cfg(feature = "early-data")]
            MidHandshake::EarlyData(stream) => Ok(Async::Ready(stream)),
            MidHandshake::End => panic!(),
        }
    }
}

impl<IO> io::Read for TlsStream<IO>
where
    IO: AsyncRead + AsyncWrite,
{
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        match self.state {
            #[cfg(feature = "early-data")]
            TlsState::EarlyData => {
                {
                    let mut stream = Stream::new(&mut self.io, &mut self.session);
                    let (pos, data) = &mut self.early_data;

                    // complete handshake
                    if stream.session.is_handshaking() {
                        stream.complete_io()?;
                    }

                    // write early data (fallback)
                    if !stream.session.is_early_data_accepted() {
                        while *pos < data.len() {
                            let len = stream.write(&data[*pos..])?;
                            *pos += len;
                        }
                    }

                    // end
                    self.state = TlsState::Stream;
                    data.clear();
                }

                self.read(buf)
            }
            TlsState::Stream | TlsState::WriteShutdown => {
                let mut stream = Stream::new(&mut self.io, &mut self.session).set_eof(!self.state.readable());

                match stream.read(buf) {
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
                }
            }
            TlsState::ReadShutdown | TlsState::FullyShutdown => Ok(0),
        }
    }
}

impl<IO> io::Write for TlsStream<IO>
where
    IO: AsyncRead + AsyncWrite,
{
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let mut stream = Stream::new(&mut self.io, &mut self.session).set_eof(!self.state.readable());

        match self.state {
            #[cfg(feature = "early-data")]
            TlsState::EarlyData => {
                let (pos, data) = &mut self.early_data;

                // write early data
                if let Some(mut early_data) = stream.session.early_data() {
                    let len = early_data.write(buf)?;
                    data.extend_from_slice(&buf[..len]);
                    return Ok(len);
                }

                // complete handshake
                if stream.session.is_handshaking() {
                    stream.complete_io()?;
                }

                // write early data (fallback)
                if !stream.session.is_early_data_accepted() {
                    while *pos < data.len() {
                        let len = stream.write(&data[*pos..])?;
                        *pos += len;
                    }
                }

                // end
                self.state = TlsState::Stream;
                data.clear();
                stream.write(buf)
            }
            _ => stream.write(buf),
        }
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
        try_nb!(stream.flush());
        stream.io.shutdown()
    }
}
