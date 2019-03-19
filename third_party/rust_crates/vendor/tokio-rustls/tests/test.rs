#[macro_use] extern crate lazy_static;
extern crate rustls;
extern crate tokio;
extern crate tokio_rustls;
extern crate webpki;

use std::{ io, thread };
use std::io::{ BufReader, Cursor };
use std::sync::Arc;
use std::sync::mpsc::channel;
use std::net::SocketAddr;
use tokio::net::{ TcpListener, TcpStream };
use rustls::{ ServerConfig, ClientConfig };
use rustls::internal::pemfile::{ certs, rsa_private_keys };
use tokio_rustls::{ TlsConnector, TlsAcceptor };

const CERT: &str = include_str!("end.cert");
const CHAIN: &str = include_str!("end.chain");
const RSA: &str = include_str!("end.rsa");

lazy_static!{
    static ref TEST_SERVER: (SocketAddr, &'static str, &'static str) = {
        use tokio::prelude::*;
        use tokio::io as aio;

        let cert = certs(&mut BufReader::new(Cursor::new(CERT))).unwrap();
        let mut keys = rsa_private_keys(&mut BufReader::new(Cursor::new(RSA))).unwrap();

        let mut config = ServerConfig::new(rustls::NoClientAuth::new());
        config.set_single_cert(cert, keys.pop().unwrap())
            .expect("invalid key or certificate");
        let config = TlsAcceptor::from(Arc::new(config));

        let (send, recv) = channel();

        thread::spawn(move || {
            let addr = SocketAddr::from(([127, 0, 0, 1], 0));
            let listener = TcpListener::bind(&addr).unwrap();

            send.send(listener.local_addr().unwrap()).unwrap();

            let done = listener.incoming()
                .for_each(move |stream| {
                    let done = config.accept(stream)
                        .and_then(|stream| {
                            let (reader, writer) = stream.split();
                            aio::copy(reader, writer)
                        })
                        .then(|_| Ok(()));

                    tokio::spawn(done);
                    Ok(())
                })
                .map_err(|err| panic!("{:?}", err));

            tokio::run(done);
        });

        let addr = recv.recv().unwrap();
        (addr, "localhost", CHAIN)
    };
}


fn start_server() -> &'static (SocketAddr, &'static str, &'static str) {
    &*TEST_SERVER
}

fn start_client(addr: &SocketAddr, domain: &str, chain: &str) -> io::Result<()> {
    use tokio::prelude::*;
    use tokio::io as aio;

    const FILE: &'static [u8] = include_bytes!("../README.md");

    let domain = webpki::DNSNameRef::try_from_ascii_str(domain).unwrap();
    let mut config = ClientConfig::new();
    let mut chain = BufReader::new(Cursor::new(chain));
    config.root_store.add_pem_file(&mut chain).unwrap();
    let config = TlsConnector::from(Arc::new(config));

    let done = TcpStream::connect(addr)
        .and_then(|stream| config.connect(domain, stream))
        .and_then(|stream| aio::write_all(stream, FILE))
        .and_then(|(stream, _)| aio::read_exact(stream, vec![0; FILE.len()]))
        .and_then(|(stream, buf)| {
            assert_eq!(buf, FILE);
            aio::shutdown(stream)
        })
        .map(drop);

    done.wait()
}

#[test]
fn pass() {
    let (addr, domain, chain) = start_server();

    start_client(addr, domain, chain).unwrap();
}

#[test]
fn fail() {
    let (addr, domain, chain) = start_server();

    assert_ne!(domain, &"google.com");
    assert!(start_client(addr, "google.com", chain).is_err());
}
