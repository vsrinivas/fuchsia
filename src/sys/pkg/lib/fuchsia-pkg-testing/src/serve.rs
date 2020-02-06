// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test tools for serving TUF repositories.

use {
    crate::repo::Repository,
    anyhow::{format_err, Error},
    bytes::Buf,
    chrono::Utc,
    fidl_fuchsia_pkg_ext::RepositoryConfig,
    fuchsia_async::{self as fasync, net::TcpListener, EHandle},
    fuchsia_url::pkg_url::RepoUrl,
    fuchsia_zircon as zx,
    futures::{
        compat::{AsyncRead01CompatExt, Compat, Future01CompatExt, Stream01CompatExt},
        future::{BoxFuture, RemoteHandle},
        prelude::*,
        task::SpawnExt,
    },
    http_sse::{Event, EventSender, SseResponseCreator},
    hyper::{header, service::service_fn, Body, Method, Request, Response, Server, StatusCode},
    std::{
        io::Cursor,
        net::{Ipv6Addr, SocketAddr},
        path::{Path, PathBuf},
        pin::Pin,
        sync::Arc,
    },
};

pub mod handler;

trait AsyncReadWrite: AsyncRead + AsyncWrite + Send {}
impl<T> AsyncReadWrite for T where T: AsyncRead + AsyncWrite + Send {}

/// A builder to construct a test repository server.
pub struct ServedRepositoryBuilder {
    repo: Arc<Repository>,
    uri_path_override_handlers: Vec<Arc<dyn UriPathHandler>>,
    use_https: bool,
}

/// Override how a `ServedRepository` responds to GET requests on valid URI paths.
/// Useful for injecting failures.
pub trait UriPathHandler: 'static + Send + Sync {
    /// `response` is what the server would have responded with.
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>>;
}

impl ServedRepositoryBuilder {
    pub(crate) fn new(repo: Arc<Repository>) -> Self {
        ServedRepositoryBuilder { repo, uri_path_override_handlers: vec![], use_https: false }
    }

    /// Override how the `ServedRepositoryBuilder` responds to some URI paths.
    ///
    /// Requests are passed through URI path handlers in the order in which they were added to this
    /// builder.
    pub fn uri_path_override_handler(mut self, handler: impl UriPathHandler) -> Self {
        self.uri_path_override_handlers.push(Arc::new(handler));
        self
    }

    /// Serve the repository over TLS, using a server certificate rooted to
    /// //third_party/rust_crates/vendor/rustls/test-ca/rsa/ca.cert.
    pub fn use_https(mut self, value: bool) -> Self {
        self.use_https = value;
        self
    }

    /// Spawn the server on the current executor, returning a handle to manage the server.
    pub fn start(self) -> Result<ServedRepository, Error> {
        let (listener, addr) = {
            let addr = SocketAddr::new(Ipv6Addr::UNSPECIFIED.into(), 0);
            let listener = TcpListener::bind(&addr)?;
            let local_addr = listener.local_addr()?;
            (listener, local_addr)
        };

        let listener = listener.accept_stream().map_err(Error::from).map_ok(|(conn, _addr)| conn);

        let connections: Compat<
            Pin<Box<dyn Stream<Item = Result<Compat<Pin<Box<dyn AsyncReadWrite>>>, Error>> + Send>>,
        > = if self.use_https {
            // build a server configuration using a test CA and cert chain
            let certs = parse_cert_chain(&include_bytes!("../certs/server.certchain")[..]);
            let key = parse_private_key(&include_bytes!("../certs/server.rsa")[..]);
            let mut tls_config = rustls::ServerConfig::new(rustls::NoClientAuth::new());
            tls_config.set_single_cert(certs, key).unwrap();
            let tls_acceptor = tokio_rustls::TlsAcceptor::from(Arc::new(tls_config));

            // wrap incoming tcp streams
            listener
                .and_then(move |conn| {
                    tls_acceptor.accept(conn.compat()).compat().map(|res| match res {
                        Ok(conn) => Ok((Pin::new(Box::new(conn.compat()))
                            as Pin<Box<dyn AsyncReadWrite>>)
                            .compat()),
                        Err(e) => Err(Error::from(e)),
                    })
                })
                .boxed()
                .compat()
        } else {
            listener
                .map_ok(|conn| (Pin::new(Box::new(conn)) as Pin<Box<dyn AsyncReadWrite>>).compat())
                .boxed()
                .compat()
        };

        let root = self.repo.path();
        let uri_path_override_handlers = Arc::new(self.uri_path_override_handlers);

        let (auto_response_creator, auto_event_sender) =
            SseResponseCreator::with_additional_buffer_size(10);
        let auto_response_creator = Arc::new(auto_response_creator);

        let service = move || {
            let root = root.clone();
            let uri_path_override_handlers = Arc::clone(&uri_path_override_handlers);
            let auto_response_creator = Arc::clone(&auto_response_creator);

            service_fn(move |req| {
                let method = req.method().to_owned();
                let path = req.uri().path().to_owned();
                ServedRepository::handle_tuf_repo_request(
                    root.clone(),
                    Arc::clone(&uri_path_override_handlers),
                    Arc::clone(&auto_response_creator),
                    req,
                )
                .map(move |x| -> Result<Response<Body>, hyper::Error> {
                    println!(
                        "{} [http repo] {} {} => {}",
                        Utc::now().format("%T.%6f"),
                        method,
                        path,
                        x.status()
                    );
                    Ok(x)
                })
                .boxed()
                .compat()
            })
        };

        let (stop, rx_stop) = futures::channel::oneshot::channel();

        let (server, wait_stop) = Server::builder(connections)
            .executor(EHandle::local().compat())
            .serve(service)
            .with_graceful_shutdown(rx_stop.compat())
            .compat()
            .unwrap_or_else(|e| panic!("error serving repo over http: {}", e))
            .remote_handle();

        fasync::spawn(server);

        Ok(ServedRepository {
            repo: self.repo,
            stop,
            wait_stop,
            addr,
            use_https: self.use_https,
            auto_event_sender,
        })
    }
}

fn parse_cert_chain(mut bytes: &[u8]) -> Vec<rustls::Certificate> {
    rustls::internal::pemfile::certs(&mut bytes).expect("certs to parse")
}

fn parse_private_key(mut bytes: &[u8]) -> rustls::PrivateKey {
    let keys =
        rustls::internal::pemfile::rsa_private_keys(&mut bytes).expect("private keys to parse");
    assert_eq!(keys.len(), 1, "expecting a single private key");
    keys.into_iter().next().unwrap()
}

/// A [`Repository`] being served over HTTP.
pub struct ServedRepository {
    repo: Arc<Repository>,
    stop: futures::channel::oneshot::Sender<()>,
    wait_stop: RemoteHandle<()>,
    addr: SocketAddr,
    use_https: bool,
    auto_event_sender: EventSender,
}

impl ServedRepository {
    fn scheme(&self) -> &'static str {
        if self.use_https {
            "https"
        } else {
            "http"
        }
    }
    /// Request the given path served by the repository over HTTP.
    pub async fn get(&self, path: impl AsRef<str>) -> Result<Vec<u8>, Error> {
        let url = format!("{}/{}", self.local_url(), path.as_ref());
        get(url).await
    }

    /// Returns the URL that can be used to connect to this repository from this device.
    pub fn local_url(&self) -> String {
        format!("{}://localhost:{}", self.scheme(), self.addr.port())
    }

    /// Returns a sorted vector of all packages contained in this repository.
    pub async fn list_packages(&self) -> Result<Vec<crate::repo::PackageEntry>, Error> {
        let targets_json = self.get("targets.json").await?;
        let mut packages = crate::repo::iter_packages(Cursor::new(targets_json))?
            .collect::<Result<Vec<_>, _>>()?;
        packages.sort_unstable();
        Ok(packages)
    }

    /// Generate a [`RepositoryConfig`] suitable for configuring a package resolver to use this
    /// served repository.
    pub fn make_repo_config(&self, url: RepoUrl) -> RepositoryConfig {
        self.repo.make_repo_config(url, self.local_url(), false)
    }

    /// Generate a [`RepositoryConfig`] suitable for configuring a package resolver to use this
    /// served repository. Set subscribe on the mirror configs to true.
    pub fn make_repo_config_with_subscribe(&self, url: RepoUrl) -> RepositoryConfig {
        self.repo.make_repo_config(url, self.local_url(), true)
    }

    /// Send an SSE event to all clients subscribed to /auto.
    pub async fn send_auto_event(&self, event: &Event) {
        self.auto_event_sender.send(event).await
    }

    /// Waits until `send_auto_event` would attempt to send an `Event` to exactly
    /// `n` clients. Panics if extra clients are connected.
    pub async fn wait_for_n_connected_auto_clients(&self, n: usize) {
        loop {
            let connected = self.auto_event_sender.client_count().await;
            if connected == n {
                break;
            } else if connected > n {
                panic!("ServedRepository too many auto clients connected.");
            }
            fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(10))).await;
        }
    }

    /// Errors all currently existing /auto `Response<Body>` streams.
    pub async fn drop_all_auto_clients(&self) {
        self.auto_event_sender.drop_all_clients().await
    }

    /// Gracefully signal the server to stop and returns a future that resolves when it terminates.
    pub fn stop(self) -> RemoteHandle<()> {
        self.stop.send(()).expect("remote end to still be open");
        self.wait_stop
    }

    async fn handle_tuf_repo_request(
        repo: PathBuf,
        uri_path_override_handlers: Arc<Vec<Arc<dyn UriPathHandler>>>,
        auto_response_creator: Arc<SseResponseCreator>,
        req: Request<Body>,
    ) -> Response<Body> {
        let fail =
            |status: StatusCode| Response::builder().status(status).body(Body::empty()).unwrap();

        if *req.method() != Method::GET {
            return fail(StatusCode::NOT_FOUND);
        } else if req.uri().query().is_some() {
            return fail(StatusCode::BAD_REQUEST);
        }

        let uri_path = Path::new(req.uri().path());

        // don't let queries escape the repo root.
        if uri_path.components().any(|component| component == std::path::Component::ParentDir) {
            return fail(StatusCode::NOT_FOUND);
        }

        let mut response = if uri_path == PathBuf::from("/auto") {
            auto_response_creator.create().await
        } else {
            let fs_path = repo.join(uri_path.strip_prefix("/").unwrap_or(uri_path));
            // FIXME synchronous IO in an async context.
            let data = match std::fs::read(fs_path) {
                Ok(data) => data,
                Err(ref err) if err.kind() == std::io::ErrorKind::NotFound => {
                    return fail(StatusCode::NOT_FOUND);
                }
                Err(err) => {
                    eprintln!("error reading repo file: {}", err);
                    return fail(StatusCode::INTERNAL_SERVER_ERROR);
                }
            };

            Response::builder()
                .status(StatusCode::OK)
                .header(header::CONTENT_LENGTH, data.len())
                .body(Body::from(data))
                .unwrap()
        };

        for handler in uri_path_override_handlers.iter() {
            response = handler.handle(uri_path, response).await
        }

        return response;
    }
}

async fn get(url: impl AsRef<str>) -> Result<Vec<u8>, Error> {
    let request = Request::get(url.as_ref()).body(Body::empty()).map_err(|e| Error::from(e))?;
    let client = fuchsia_hyper::new_client();
    let response = client.request(request).compat().await?;

    if response.status() != StatusCode::OK {
        return Err(format_err!("unexpected status code: {:?}", response.status()));
    }

    let body = response.into_body().compat().try_concat().await?.collect();

    Ok(body)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{package::PackageBuilder, repo::RepositoryBuilder},
        matches::assert_matches,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_serve_empty() {
        let repo = Arc::new(RepositoryBuilder::new().build().await.unwrap());
        let served_repo = repo.server().start().unwrap();

        // contains no packages.
        let packages = served_repo.list_packages().await.unwrap();
        assert_eq!(packages, vec![]);

        // no '..' allowed.
        assert_matches!(served_repo.get("blobs/../root.json").await, Err(_));

        // getting a known file fetches something.
        let bytes = served_repo.get("targets.json").await.unwrap();
        assert_ne!(bytes, Vec::<u8>::new());

        // even if it doesn't go through the helper function.
        let url = format!("{}/targets.json", served_repo.local_url());
        let also_bytes = get(&url).await.unwrap();
        assert_eq!(bytes, also_bytes);

        // requests fail after stopping the server
        served_repo.stop().await;
        assert_matches!(get(url).await, Err(_));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_serve_packages() {
        let same_contents = "same contents";
        let repo = RepositoryBuilder::new()
            .add_package(
                PackageBuilder::new("rolldice")
                    .add_resource_at("bin/rolldice", "#!/boot/bin/sh\necho 4\n".as_bytes())
                    .add_resource_at(
                        "meta/rolldice.cmx",
                        r#"{"program":{"binary":"bin/rolldice"}}"#.as_bytes(),
                    )
                    .add_resource_at("data/duplicate_a", "same contents".as_bytes())
                    .build()
                    .await
                    .unwrap(),
            )
            .add_package(
                PackageBuilder::new("fortune")
                    .add_resource_at(
                        "bin/fortune",
                        "#!/boot/bin/sh\necho ask again later\n".as_bytes(),
                    )
                    .add_resource_at(
                        "meta/fortune.cmx",
                        r#"{"program":{"binary":"bin/fortune"}}"#.as_bytes(),
                    )
                    .add_resource_at("data/duplicate_b", same_contents.as_bytes())
                    .add_resource_at("data/duplicate_c", same_contents.as_bytes())
                    .build()
                    .await
                    .unwrap(),
            )
            .build()
            .await
            .unwrap();
        let repo = Arc::new(repo);

        let local_packages = repo.list_packages().unwrap();

        let served_repository = repo.server().start().unwrap();
        let served_packages = served_repository.list_packages().await.unwrap();
        assert_eq!(local_packages, served_packages);
    }
}
