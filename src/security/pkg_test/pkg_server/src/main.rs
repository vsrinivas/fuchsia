// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    argh::FromArgs,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, CLONE_FLAG_SAME_RIGHTS, OPEN_RIGHT_READABLE,
    },
    fidl_test_security_pkg::{PackageServer_Request, PackageServer_RequestStream},
    fuchsia_async::{net::TcpListener, Task},
    fuchsia_component::server::ServiceFs,
    fuchsia_hyper::{Executor, TcpStream},
    fuchsia_syslog::{fx_log_info, fx_log_warn, init},
    futures::{
        channel::oneshot::{channel, Receiver},
        stream::{Stream, StreamExt, TryStreamExt},
        FutureExt,
    },
    hyper::{
        server::{accept::from_stream, Server},
        service::{make_service_fn, service_fn},
        Body, Method, Request, Response, StatusCode,
    },
    io_util::{open_directory_in_namespace, open_file, open_file_in_namespace, read_file_bytes},
    rustls::{Certificate, NoClientAuth, ServerConfig},
    std::{
        net::{IpAddr, Ipv4Addr, SocketAddr},
        path::Path,
        pin::Pin,
        sync::Arc,
    },
    tokio::io::{AsyncRead, AsyncWrite},
    tokio_rustls::TlsAcceptor,
};

/// Flags for pkg_server.
#[derive(FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to only root SSL certificates file.
    #[argh(option)]
    tls_certificate_chain_path: String,
    /// absolute path to TLS private key for HTTPS server.
    #[argh(option)]
    tls_private_key_path: String,
    /// absolute path to directory to serve over HTTPS.
    #[argh(option)]
    repository_path: String,
}

trait AsyncReadWrite: AsyncRead + AsyncWrite + Send {}
impl<T> AsyncReadWrite for T where T: AsyncRead + AsyncWrite + Send {}

fn parse_cert_chain(mut bytes: &[u8]) -> Vec<Certificate> {
    rustls::internal::pemfile::certs(&mut bytes).expect("certs to parse")
}

fn parse_private_key(mut bytes: &[u8]) -> rustls::PrivateKey {
    let keys =
        rustls::internal::pemfile::rsa_private_keys(&mut bytes).expect("private keys to parse");
    assert_eq!(keys.len(), 1, "expecting a single private key");
    keys.into_iter().next().unwrap()
}

struct RequestHandler {
    repository_dir: DirectoryProxy,
}

impl RequestHandler {
    pub fn new(repository_dir_ref: &DirectoryProxy) -> Self {
        let (repository_dir, server_end) = create_proxy::<DirectoryMarker>().unwrap();
        let server_end = server_end.into_channel().into();
        repository_dir_ref.clone(CLONE_FLAG_SAME_RIGHTS, server_end).unwrap();
        Self { repository_dir }
    }

    pub async fn handle_request(&self, req: Request<Body>) -> Result<Response<Body>> {
        match (req.method(), req.uri().path()) {
            (&Method::GET, path) => self.simple_file_send(path).await,
            (_, path) => Self::not_found(path, None),
        }
    }

    fn not_found(path: &str, err: Option<Error>) -> Result<Response<Body>> {
        match err {
            Some(err) => fx_log_warn!("Not found: {}: {:?}", path, err),
            None => fx_log_warn!("Not found: {}", path),
        }
        Response::builder()
            .status(StatusCode::NOT_FOUND)
            .body("Not found".into())
            .map_err(Error::from)
    }

    fn ok(path: &str, body: Vec<u8>) -> Result<Response<Body>> {
        fx_log_info!("OK: {}", path);
        Response::builder().status(StatusCode::OK).body(body.into()).map_err(Error::from)
    }

    async fn simple_file_send(&self, path: &str) -> Result<Response<Body>> {
        // Drop leading "/" from path.
        assert!(path.starts_with("/"));
        let mut path_chars = path.chars();
        path_chars.next();
        let path = path_chars.as_str();

        match open_file(&self.repository_dir, Path::new(path), OPEN_RIGHT_READABLE) {
            Ok(file) => match read_file_bytes(&file).await {
                Ok(bytes) => Self::ok(path, bytes),
                Err(err) => Self::not_found(path, Some(err)),
            },
            Err(err) => Self::not_found(path, Some(err)),
        }
    }
}

fn serve_package_server_protocol(url_recv: Receiver<String>) {
    let local_url = url_recv.shared();
    Task::spawn(async move {
        fx_log_info!("Preparing to serve test.security.pkg.PackageServer");
        let mut fs = ServiceFs::new();
        fs.dir("svc").add_fidl_service(move |mut stream: PackageServer_RequestStream| {
            let local_url = local_url.clone();
            fx_log_info!("New connection to test.security.pkg.PackageServer");
            Task::spawn(async move {
                while let Some(request) = stream.try_next().await.unwrap() {
                    let local_url = local_url.clone();
                    match request {
                        PackageServer_Request::GetUrl { responder } => {
                            let local_url = local_url.await.unwrap();
                            fx_log_info!(
                                "Responding to test.security.pkg.PackageServer.GetUrl request with {}",
                                local_url
                            );
                            responder.send(&local_url).unwrap();
                        }
                    }
                }
            })
            .detach();
        });
        fs.take_and_serve_directory_handle().unwrap();
        fs.collect::<()>().await;
    })
    .detach()
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    init().unwrap();
    fx_log_info!("Starting pkg_server");
    let args @ Args { tls_certificate_chain_path, tls_private_key_path, repository_path } =
        &argh::from_env();
    fx_log_info!("Initalizing pkg_server with {:?}", args);

    let (url_send, url_recv) = channel();
    serve_package_server_protocol(url_recv);

    let root_ssl_certificates_file =
        open_file_in_namespace(tls_certificate_chain_path, OPEN_RIGHT_READABLE).unwrap();
    let root_ssl_certificates_contents =
        read_file_bytes(&root_ssl_certificates_file).await.unwrap();

    let tls_private_key_file =
        open_file_in_namespace(tls_private_key_path, OPEN_RIGHT_READABLE).unwrap();
    let tls_private_key_contents = read_file_bytes(&tls_private_key_file).await.unwrap();

    let certs = parse_cert_chain(root_ssl_certificates_contents.as_slice());
    let key = parse_private_key(tls_private_key_contents.as_slice());

    let mut tls_config = ServerConfig::new(NoClientAuth::new());
    // Configure ALPN and prefer H2 over HTTP/1.1.
    tls_config.set_protocols(&[b"h2".to_vec(), b"http/1.1".to_vec()]);
    tls_config.set_single_cert(certs, key).unwrap();
    let tls_acceptor = TlsAcceptor::from(Arc::new(tls_config));

    let (listener, addr) = {
        let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 443);
        let listener = TcpListener::bind(&addr).unwrap();
        let local_addr = listener.local_addr().unwrap();
        (listener, local_addr)
    };

    let listener = listener
        .accept_stream()
        .map_err(Error::from)
        .map_ok(|(conn, _addr)| TcpStream { stream: conn });

    let connections: Pin<
        Box<dyn Stream<Item = Result<Pin<Box<dyn AsyncReadWrite>>, Error>> + Send>,
    > = listener
        .and_then(move |conn| {
            tls_acceptor.accept(conn).map(|res| match res {
                Ok(conn) => Ok(Pin::new(Box::new(conn)) as Pin<Box<dyn AsyncReadWrite>>),
                Err(e) => Err(Error::from(e)),
            })
        })
        .boxed();

    let make_service = make_service_fn(|_| {
        let repository_dir =
            open_directory_in_namespace(repository_path, OPEN_RIGHT_READABLE).unwrap();
        async move {
            Ok::<_, Error>(service_fn(move |req: Request<Body>| {
                let handler = RequestHandler::new(&repository_dir);
                async move { handler.handle_request(req).await }
            }))
        }
    });

    let server: Server<_, _, Executor> =
        Server::builder(from_stream(connections)).executor(Executor).serve(make_service);

    fx_log_info!("pkg_server listening on {}", addr);

    url_send.send("https://localhost".to_string()).unwrap();

    server.await.unwrap();
}
