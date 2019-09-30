mod vecbuf;

use std::io::{ self, Read, Write };
use rustls::Session;
use rustls::WriteV;
use tokio_io::{ AsyncRead, AsyncWrite };

pub struct Stream<'a, IO: 'a, S: 'a> {
    pub io: &'a mut IO,
    pub session: &'a mut S,
    pub eof: bool,
}

pub trait WriteTls<'a, IO: AsyncRead + AsyncWrite, S: Session>: Read + Write {
    fn write_tls(&mut self) -> io::Result<usize>;
}

#[derive(Clone, Copy)]
enum Focus {
    Empty,
    Readable,
    Writable
}

impl<'a, IO: AsyncRead + AsyncWrite, S: Session> Stream<'a, IO, S> {
    pub fn new(io: &'a mut IO, session: &'a mut S) -> Self {
        Stream {
            io,
            session,
            // The state so far is only used to detect EOF, so either Stream
            // or EarlyData state should both be all right.
            eof: false,
        }
    }

    pub fn set_eof(mut self, eof: bool) -> Self {
        self.eof = eof;
        self
    }

    pub fn complete_io(&mut self) -> io::Result<(usize, usize)> {
        self.complete_inner_io(Focus::Empty)
    }

    fn complete_read_io(&mut self) -> io::Result<usize> {
        let n = self.session.read_tls(self.io)?;

        self.session.process_new_packets()
            .map_err(|err| {
                // In case we have an alert to send describing this error,
                // try a last-gasp write -- but don't predate the primary
                // error.
                let _ = self.write_tls();

                io::Error::new(io::ErrorKind::InvalidData, err)
            })?;

        Ok(n)
    }

    fn complete_write_io(&mut self) -> io::Result<usize> {
        self.write_tls()
    }

    fn complete_inner_io(&mut self, focus: Focus) -> io::Result<(usize, usize)> {
        let mut wrlen = 0;
        let mut rdlen = 0;

        loop {
            let mut write_would_block = false;
            let mut read_would_block = false;

            while self.session.wants_write() {
                match self.complete_write_io() {
                    Ok(n) => wrlen += n,
                    Err(ref err) if err.kind() == io::ErrorKind::WouldBlock => {
                        write_would_block = true;
                        break
                    },
                    Err(err) => return Err(err)
                }
            }

            if let Focus::Writable = focus {
                if !write_would_block {
                    return Ok((rdlen, wrlen));
                } else {
                    return Err(io::ErrorKind::WouldBlock.into());
                }
            }

            if !self.eof && self.session.wants_read() {
                match self.complete_read_io() {
                    Ok(0) => self.eof = true,
                    Ok(n) => rdlen += n,
                    Err(ref err) if err.kind() == io::ErrorKind::WouldBlock => {
                        read_would_block = true
                    }
                    Err(err) => return Err(err),
                }
            }

            let would_block = match focus {
                Focus::Empty => write_would_block || read_would_block,
                Focus::Readable => read_would_block,
                Focus::Writable => write_would_block,
            };

            match (
                self.eof,
                self.session.is_handshaking(),
                would_block,
            ) {
                (true, true, _) => return Err(io::ErrorKind::UnexpectedEof.into()),
                (_, false, true) => {
                    let would_block = match focus {
                        Focus::Empty => rdlen == 0 && wrlen == 0,
                        Focus::Readable => rdlen == 0,
                        Focus::Writable => wrlen == 0
                    };

                    return if would_block {
                        Err(io::ErrorKind::WouldBlock.into())
                    } else {
                        Ok((rdlen, wrlen))
                    };
                },
                (_, false, _) => return Ok((rdlen, wrlen)),
                (_, true, true) => return Err(io::ErrorKind::WouldBlock.into()),
                (..) => ()
            }
        }
    }
}

impl<'a, IO: AsyncRead + AsyncWrite, S: Session> WriteTls<'a, IO, S> for Stream<'a, IO, S> {
    fn write_tls(&mut self) -> io::Result<usize> {
        use futures::Async;
        use self::vecbuf::VecBuf;

        struct V<'a, IO: 'a>(&'a mut IO);

        impl<'a, IO: AsyncWrite> WriteV for V<'a, IO> {
            fn writev(&mut self, vbytes: &[&[u8]]) -> io::Result<usize> {
                let mut vbytes = VecBuf::new(vbytes);
                match self.0.write_buf(&mut vbytes) {
                    Ok(Async::Ready(n)) => Ok(n),
                    Ok(Async::NotReady) => Err(io::ErrorKind::WouldBlock.into()),
                    Err(err) => Err(err)
                }
            }
        }

        let mut vecio = V(self.io);
        self.session.writev_tls(&mut vecio)
    }
}

impl<'a, IO: AsyncRead + AsyncWrite, S: Session> Read for Stream<'a, IO, S> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        while self.session.wants_read() {
            if let (0, _) = self.complete_inner_io(Focus::Readable)? {
                break
            }
        }
        self.session.read(buf)
    }
}

impl<'a, IO: AsyncRead + AsyncWrite, S: Session> Write for Stream<'a, IO, S> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let len = self.session.write(buf)?;
        while self.session.wants_write() {
            match self.complete_inner_io(Focus::Writable) {
                Ok(_) => (),
                Err(ref err) if err.kind() == io::ErrorKind::WouldBlock && len != 0 => break,
                Err(err) => return Err(err)
            }
        }

        if len != 0 || buf.is_empty() {
            Ok(len)
        } else {
            // not write zero
            self.session.write(buf)
                .and_then(|len| if len != 0 {
                    Ok(len)
                } else {
                    Err(io::ErrorKind::WouldBlock.into())
                })
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        self.session.flush()?;
        while self.session.wants_write() {
            self.complete_inner_io(Focus::Writable)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod test_stream;
