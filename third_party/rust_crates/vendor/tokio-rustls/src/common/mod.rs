#[cfg(feature = "nightly")]
#[cfg(feature = "tokio-support")]
mod vecbuf;

use std::io::{ self, Read, Write };
#[cfg(feature = "nightly")]
use std::io::Initializer;
use rustls::Session;
#[cfg(feature = "nightly")]
use rustls::WriteV;
#[cfg(feature = "nightly")]
#[cfg(feature = "tokio-support")]
use tokio_io::AsyncWrite;


pub struct Stream<'a, S: 'a, IO: 'a> {
    pub session: &'a mut S,
    pub io: &'a mut IO
}

pub trait WriteTls<'a, S: Session, IO: Read + Write>: Read + Write {
    fn write_tls(&mut self) -> io::Result<usize>;
}

impl<'a, S: Session, IO: Read + Write> Stream<'a, S, IO> {
    pub fn new(session: &'a mut S, io: &'a mut IO) -> Self {
        Stream { session, io }
    }

    pub fn complete_io(&mut self) -> io::Result<(usize, usize)> {
        // fork from https://github.com/ctz/rustls/blob/master/src/session.rs#L161

        let until_handshaked = self.session.is_handshaking();
        let mut eof = false;
        let mut wrlen = 0;
        let mut rdlen = 0;

        loop {
            while self.session.wants_write() {
                wrlen += self.write_tls()?;
            }

            if !until_handshaked && wrlen > 0 {
                return Ok((rdlen, wrlen));
            }

            if !eof && self.session.wants_read() {
                match self.session.read_tls(self.io)? {
                    0 => eof = true,
                    n => rdlen += n
                }
            }

            match self.session.process_new_packets() {
                Ok(_) => {},
                Err(e) => {
                    // In case we have an alert to send describing this error,
                    // try a last-gasp write -- but don't predate the primary
                    // error.
                    let _ignored = self.write_tls();

                    return Err(io::Error::new(io::ErrorKind::InvalidData, e));
                },
            };

            match (eof, until_handshaked, self.session.is_handshaking()) {
                (_, true, false) => return Ok((rdlen, wrlen)),
                (_, false, _) => return Ok((rdlen, wrlen)),
                (true, true, true) => return Err(io::Error::from(io::ErrorKind::UnexpectedEof)),
                (..) => ()
            }
        }
    }
}

#[cfg(not(feature = "nightly"))]
impl<'a, S: Session, IO: Read + Write> WriteTls<'a, S, IO> for Stream<'a, S, IO> {
    fn write_tls(&mut self) -> io::Result<usize> {
        self.session.write_tls(self.io)
    }
}

#[cfg(feature = "nightly")]
impl<'a, S: Session, IO: Read + Write> WriteTls<'a, S, IO> for Stream<'a, S, IO> {
    default fn write_tls(&mut self) -> io::Result<usize> {
        self.session.write_tls(self.io)
    }
}

#[cfg(feature = "nightly")]
#[cfg(feature = "tokio-support")]
impl<'a, S: Session, IO: Read + AsyncWrite> WriteTls<'a, S, IO> for Stream<'a, S, IO> {
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

        let mut vecbuf = V(self.io);
        self.session.writev_tls(&mut vecbuf)
    }
}

impl<'a, S: Session, IO: Read + Write> Read for Stream<'a, S, IO> {
    #[cfg(feature = "nightly")]
    unsafe fn initializer(&self) -> Initializer {
        Initializer::nop()
    }

    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        while self.session.wants_read() {
            if let (0, 0) = self.complete_io()? {
                break
            }
        }
        self.session.read(buf)
    }
}

impl<'a, S: Session, IO: Read + Write> io::Write for Stream<'a, S, IO> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let len = self.session.write(buf)?;
        while self.session.wants_write() {
            match self.complete_io() {
                Ok(_) => (),
                Err(ref err) if err.kind() == io::ErrorKind::WouldBlock && len != 0 => break,
                Err(err) => return Err(err)
            }
        }
        Ok(len)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.session.flush()?;
        if self.session.wants_write() {
            self.complete_io()?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod test_stream;
