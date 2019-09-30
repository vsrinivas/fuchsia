use std::sync::Arc;
use std::io::{ self, Read, Write, BufReader, Cursor };
use webpki::DNSNameRef;
use rustls::internal::pemfile::{ certs, rsa_private_keys };
use rustls::{
    ServerConfig, ClientConfig,
    ServerSession, ClientSession,
    Session, NoClientAuth
};
use futures::{ Async, Poll };
use tokio_io::{ AsyncRead, AsyncWrite };
use super::Stream;

struct Good<'a>(&'a mut Session);

impl<'a> Read for Good<'a> {
    fn read(&mut self, mut buf: &mut [u8]) -> io::Result<usize> {
        self.0.write_tls(buf.by_ref())
    }
}

impl<'a> Write for Good<'a> {
    fn write(&mut self, mut buf: &[u8]) -> io::Result<usize> {
        let len = self.0.read_tls(buf.by_ref())?;
        self.0.process_new_packets()
            .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;
        Ok(len)
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl<'a> AsyncRead for Good<'a> {}
impl<'a> AsyncWrite for Good<'a> {
    fn shutdown(&mut self) -> Poll<(), io::Error> {
        Ok(Async::Ready(()))
    }
}

struct Bad(bool);

impl Read for Bad {
    fn read(&mut self, _: &mut [u8]) -> io::Result<usize> {
        Ok(0)
    }
}

impl Write for Bad {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        if self.0 {
            Err(io::ErrorKind::WouldBlock.into())
        } else {
            Ok(buf.len())
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl AsyncRead for Bad {}
impl AsyncWrite for Bad {
    fn shutdown(&mut self) -> Poll<(), io::Error> {
        Ok(Async::Ready(()))
    }
}


#[test]
fn stream_good() -> io::Result<()> {
    const FILE: &'static [u8] = include_bytes!("../../README.md");

    let (mut server, mut client) = make_pair();
    do_handshake(&mut client, &mut server);
    io::copy(&mut Cursor::new(FILE), &mut server)?;

    {
        let mut good = Good(&mut server);
        let mut stream = Stream::new(&mut good, &mut client);

        let mut buf = Vec::new();
        stream.read_to_end(&mut buf)?;
        assert_eq!(buf, FILE);
        stream.write_all(b"Hello World!")?;
    }

    let mut buf = String::new();
    server.read_to_string(&mut buf)?;
    assert_eq!(buf, "Hello World!");

    Ok(())
}

#[test]
fn stream_bad() -> io::Result<()> {
    let (mut server, mut client) = make_pair();
    do_handshake(&mut client, &mut server);
    client.set_buffer_limit(1024);

    let mut bad = Bad(true);
    let mut stream = Stream::new(&mut bad, &mut client);
    assert_eq!(stream.write(&[0x42; 8])?, 8);
    assert_eq!(stream.write(&[0x42; 8])?, 8);
    let r = stream.write(&[0x00; 1024])?; // fill buffer
    assert!(r < 1024);
    assert_eq!(
        stream.write(&[0x01]).unwrap_err().kind(),
        io::ErrorKind::WouldBlock
    );

    Ok(())
}

#[test]
fn stream_handshake() -> io::Result<()> {
    let (mut server, mut client) = make_pair();

    {
        let mut good = Good(&mut server);
        let mut stream = Stream::new(&mut good, &mut client);
        let (r, w) = stream.complete_io()?;

        assert!(r > 0);
        assert!(w > 0);

        stream.complete_io()?; // finish server handshake
    }

    assert!(!server.is_handshaking());
    assert!(!client.is_handshaking());

    Ok(())
}

#[test]
fn stream_handshake_eof() -> io::Result<()> {
    let (_, mut client) = make_pair();

    let mut bad = Bad(false);
    let mut stream = Stream::new(&mut bad, &mut client);
    let r = stream.complete_io();

    assert_eq!(r.unwrap_err().kind(), io::ErrorKind::UnexpectedEof);

    Ok(())
}

#[test]
fn stream_eof() -> io::Result<()> {
    let (mut server, mut client) = make_pair();
    do_handshake(&mut client, &mut server);
    {
        let mut good = Good(&mut server);
        let mut stream = Stream::new(&mut good, &mut client).set_eof(true);
        let (r, _) = stream.complete_io()?;
        assert!(r == 0);
    }
    Ok(())
}

fn make_pair() -> (ServerSession, ClientSession) {
    const CERT: &str = include_str!("../../tests/end.cert");
    const CHAIN: &str = include_str!("../../tests/end.chain");
    const RSA: &str = include_str!("../../tests/end.rsa");

    let cert = certs(&mut BufReader::new(Cursor::new(CERT))).unwrap();
    let mut keys = rsa_private_keys(&mut BufReader::new(Cursor::new(RSA))).unwrap();
    let mut sconfig = ServerConfig::new(NoClientAuth::new());
    sconfig.set_single_cert(cert, keys.pop().unwrap()).unwrap();
    let server = ServerSession::new(&Arc::new(sconfig));

    let domain = DNSNameRef::try_from_ascii_str("localhost").unwrap();
    let mut cconfig = ClientConfig::new();
    let mut chain = BufReader::new(Cursor::new(CHAIN));
    cconfig.root_store.add_pem_file(&mut chain).unwrap();
    let client = ClientSession::new(&Arc::new(cconfig), domain);

    (server, client)
}

fn do_handshake(client: &mut ClientSession, server: &mut ServerSession) {
    let mut good = Good(server);
    let mut stream = Stream::new(&mut good, client);
    stream.complete_io().unwrap();
    stream.complete_io().unwrap();
}
