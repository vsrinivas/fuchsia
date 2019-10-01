// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test tools for serving TUF repositories.

use {
    crate::repo::{get, Repository},
    failure::Error,
    fidl_fuchsia_pkg_ext::RepositoryConfig,
    fuchsia_async::{self as fasync, net::TcpListener, EHandle},
    fuchsia_url::pkg_url::RepoUrl,
    futures::{
        compat::Future01CompatExt,
        future::{ready, BoxFuture, RemoteHandle},
        prelude::*,
        task::SpawnExt,
    },
    hyper::{service::service_fn, Body, Method, Request, Response, Server, StatusCode},
    std::{
        net::{Ipv4Addr, SocketAddr},
        path::{Path, PathBuf},
        sync::Arc,
    },
};

/// A builder to construct a test repository server.
pub struct ServedRepositoryBuilder {
    repo: Arc<Repository>,
    uri_path_override_handler: Option<Arc<dyn UriPathHandler>>,
}

/// Override how a `ServedRepository` responds to GET requests on valid URI paths.
/// Useful for injecting failures.
pub trait UriPathHandler: 'static + Send + Sync {
    /// `response` is what the server would have responded with.
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<Response<Body>>;
}

struct PassThroughUriPathHandler;
impl UriPathHandler for PassThroughUriPathHandler {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<Response<Body>> {
        ready(response).boxed()
    }
}

impl ServedRepositoryBuilder {
    pub(crate) fn new(repo: Arc<Repository>) -> Self {
        ServedRepositoryBuilder { repo, uri_path_override_handler: None }
    }

    /// Override how the `ServedRepositoryBuilder` responds to some URI paths.
    pub fn uri_path_override_handler(mut self, handler: impl UriPathHandler) -> Self {
        self.uri_path_override_handler = Some(Arc::new(handler));
        self
    }

    /// Spawn the server on the current executor, returning a handle to manage the server.
    pub fn start(self) -> Result<ServedRepository, Error> {
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);

        let (connections, addr) = {
            let listener = TcpListener::bind(&addr)?;
            let local_addr = listener.local_addr()?;
            (listener.accept_stream().map_ok(|(conn, _addr)| conn.compat()), local_addr)
        };

        let root = self.repo.path();
        let uri_path_override_handler =
            self.uri_path_override_handler.unwrap_or(Arc::new(PassThroughUriPathHandler));

        let service = move || {
            let root = root.clone();
            let uri_path_override_handler = uri_path_override_handler.clone();

            service_fn(move |req| {
                ServedRepository::handle_tuf_repo_request(
                    root.clone(),
                    uri_path_override_handler.clone(),
                    req,
                )
                .boxed()
                .compat()
            })
        };

        let (stop, rx_stop) = futures::channel::oneshot::channel();

        let (server, wait_stop) = Server::builder(connections.compat())
            .executor(EHandle::local().compat())
            .serve(service)
            .with_graceful_shutdown(rx_stop.compat())
            .compat()
            .unwrap_or_else(|e| panic!("error serving repo over http: {}", e))
            .remote_handle();

        fasync::spawn(server);

        Ok(ServedRepository { repo: self.repo, stop, wait_stop, addr })
    }
}

/// A [`Repository`] being served over HTTP.
pub struct ServedRepository {
    repo: Arc<Repository>,
    stop: futures::channel::oneshot::Sender<()>,
    wait_stop: RemoteHandle<()>,
    addr: SocketAddr,
}

impl ServedRepository {
    /// Request the given path served by the repository over HTTP.
    pub async fn get(&self, path: impl AsRef<str>) -> Result<Vec<u8>, Error> {
        let url = format!("http://127.0.0.1:{}/{}", self.addr.port(), path.as_ref());
        get(url).await
    }

    /// Returns the URL that can be used to connect to this repository from this device.
    pub fn local_url(&self) -> String {
        format!("http://127.0.0.1:{}", self.addr.port())
    }

    /// Generate a [`RepositoryConfig`] suitable for configuring a package resolver to use this
    /// served repository.
    pub fn make_repo_config(&self, url: RepoUrl) -> RepositoryConfig {
        self.repo.make_repo_config(url, self.local_url())
    }

    /// Gracefully signal the server to stop and returns a future that resolves when it terminates.
    pub fn stop(self) -> RemoteHandle<()> {
        self.stop.send(()).expect("remote end to still be open");
        self.wait_stop
    }

    async fn handle_tuf_repo_request(
        repo: PathBuf,
        uri_path_override_handler: Arc<dyn UriPathHandler>,
        req: Request<Body>,
    ) -> Result<Response<Body>, hyper::Error> {
        let fail =
            |status: StatusCode| Response::builder().status(status).body(Body::empty()).unwrap();

        if *req.method() != Method::GET {
            return Ok(fail(StatusCode::NOT_FOUND));
        } else if req.uri().query().is_some() {
            return Ok(fail(StatusCode::BAD_REQUEST));
        }

        let uri_path = Path::new(req.uri().path());

        // don't let queries escape the repo root.
        if uri_path.components().any(|component| component == std::path::Component::ParentDir) {
            return Ok(fail(StatusCode::NOT_FOUND));
        }

        let fs_path = repo.join(uri_path.strip_prefix("/").unwrap_or(uri_path));
        // FIXME synchronous IO in an async context.
        let data = match std::fs::read(fs_path) {
            Ok(data) => data,
            Err(ref err) if err.kind() == std::io::ErrorKind::NotFound => {
                return Ok(fail(StatusCode::NOT_FOUND));
            }
            Err(err) => {
                eprintln!("error reading repo file: {}", err);
                return Ok(fail(StatusCode::INTERNAL_SERVER_ERROR));
            }
        };

        Ok(uri_path_override_handler
            .handle(
                uri_path,
                Response::builder().status(StatusCode::OK).body(Body::from(data)).unwrap(),
            )
            .await)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::repo::RepositoryBuilder, matches::assert_matches};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_serve_empty() -> Result<(), Error> {
        let repo = Arc::new(RepositoryBuilder::new().build().await?);
        let served_repo = repo.build_server().start()?;

        // no '..' allowed.
        assert_matches!(served_repo.get("blobs/../root.json").await, Err(_));

        // getting a known file fetches something.
        let bytes = served_repo.get("targets.json").await?;
        assert_ne!(bytes, Vec::<u8>::new());

        // even if it doesn't go through the helper function.
        let url = format!("{}/targets.json", served_repo.local_url());
        let also_bytes = get(&url).await?;
        assert_eq!(bytes, also_bytes);

        // requests fail after stopping the server
        served_repo.stop().await;
        assert_matches!(get(url).await, Err(_));

        Ok(())
    }
}
