extern crate tokio;
extern crate webpki;
extern crate webpki_roots;

use std::io;
use std::sync::Arc;
use std::net::ToSocketAddrs;
use self::tokio::io as aio;
use self::tokio::prelude::*;
use self::tokio::net::TcpStream;
use rustls::ClientConfig;
use ::{ TlsConnector, client::TlsStream };


fn get(config: Arc<ClientConfig>, domain: &str, rtt0: bool)
    -> io::Result<(TlsStream<TcpStream>, String)>
{
    let config = TlsConnector::from(config).early_data(rtt0);
    let input = format!("GET / HTTP/1.0\r\nHost: {}\r\n\r\n", domain);

    let addr = (domain, 443)
        .to_socket_addrs()?
        .next().unwrap();

    TcpStream::connect(&addr)
        .and_then(move |stream| {
            let domain = webpki::DNSNameRef::try_from_ascii_str(&domain).unwrap();
            config.connect(domain, stream)
        })
        .and_then(move |stream| aio::write_all(stream, input))
        .and_then(move |(stream, _)| aio::read_to_end(stream, Vec::new()))
        .map(|(stream, buf)| (stream, String::from_utf8(buf).unwrap()))
        .wait()
}

#[test]
fn test_0rtt() {
    let mut config = ClientConfig::new();
    config.root_store.add_server_trust_anchors(&webpki_roots::TLS_SERVER_ROOTS);
    config.enable_early_data = true;
    let config = Arc::new(config);
    let domain = "mozilla-modern.badssl.com";

    let (_, output) = get(config.clone(), domain, false).unwrap();
    assert!(output.contains("<title>mozilla-modern.badssl.com</title>"));

    let (io, output) = get(config.clone(), domain, true).unwrap();
    assert!(output.contains("<title>mozilla-modern.badssl.com</title>"));

    assert_eq!(io.early_data.0, 0);
}
