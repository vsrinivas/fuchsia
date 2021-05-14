// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{Error, RepositoryManager},
    anyhow::Result,
    async_net::{TcpListener, TcpStream},
    chrono::Utc,
    futures::{prelude::*, AsyncRead, AsyncWrite, TryStreamExt},
    hyper::{
        body::Body,
        server::{accept::from_stream, Server},
        service::{make_service_fn, service_fn},
        Request, Response, StatusCode,
    },
    log::{error, info, warn},
    std::{
        convert::{Infallible, TryInto},
        io,
        net::SocketAddr,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
};

/// RepositoryManager represents the web server that serves [Repositories](Repository) to a target.
pub struct RepositoryServer {
    local_addr: SocketAddr,
    stop: futures::channel::oneshot::Sender<()>,
}

impl RepositoryServer {
    /// Gracefully signal the server to stop, and returns a future that resolves when the server
    /// terminates.
    pub fn stop(self) {
        self.stop.send(()).expect("remote end to still be open");
    }

    /// The [RepositoryServer] is listening on this address.
    pub fn local_addr(&self) -> SocketAddr {
        self.local_addr
    }

    /// Returns the URL that can be used to connect to this server.
    pub fn local_url(&self) -> String {
        format!("http://{}", self.local_addr)
    }

    /// Create a [RepositoryServerBuilder].
    pub fn builder(
        addr: SocketAddr,
        repo_manager: Arc<RepositoryManager>,
    ) -> RepositoryServerBuilder {
        RepositoryServerBuilder::new(addr, repo_manager)
    }
}

/// [RepositoryServerBuilder] constructs [RepositoryServer] instances.
pub struct RepositoryServerBuilder {
    addr: SocketAddr,
    repo_manager: Arc<RepositoryManager>,
}

impl RepositoryServerBuilder {
    /// Create a new RepositoryServerBuilder.
    pub fn new(addr: SocketAddr, repo_manager: Arc<RepositoryManager>) -> Self {
        Self { addr, repo_manager }
    }

    /// Construct a web server future, and return a [RepositoryServer] to manage the server.
    /// [RepositoryServer], and return a handle to manaserver and the web server task.
    pub async fn start(self) -> Result<(impl Future<Output = ()>, RepositoryServer)> {
        let listener = TcpListener::bind(&self.addr).await?;
        let local_addr = listener.local_addr()?;

        let repo_manager = Arc::clone(&self.repo_manager);
        let make_svc = make_service_fn(move |_socket| {
            // Each connection to the server receives a separate service_fn
            // instance, and so needs its own copy of the repository manager.
            let repo_manager = Arc::clone(&repo_manager);

            async move {
                Ok::<_, Infallible>(service_fn(move |req| {
                    // Each request made by a connection is serviced by the
                    // service_fn created from this scope, which is why there is
                    // another cloning of the repository manager.
                    let method = req.method().to_string();
                    let path = req.uri().path().to_string();

                    handle_request(Arc::clone(&repo_manager), req, local_addr.clone())
                        .inspect(move |resp| {
                            info!(
                                "{} [ffx] {} {} => {}",
                                Utc::now().format("%T.%6f"),
                                method,
                                path,
                                resp.status()
                            );
                        })
                        .map(Ok::<_, Infallible>)
                }))
            }
        });

        let (stop, rx_stop) = futures::channel::oneshot::channel();

        // `listener.incoming()` borrows the listener, so we need to create a unique future that
        // owns the listener, and run the server in it.
        let server_fut = async move {
            let server = Server::builder(from_stream(listener.incoming().map_ok(HyperStream)))
                .executor(fuchsia_hyper::Executor)
                .serve(make_svc)
                .with_graceful_shutdown(
                    rx_stop.map(|res| res.unwrap_or_else(|futures::channel::oneshot::Canceled| ())),
                )
                .unwrap_or_else(|e| panic!("unable to start server: {}", e));

            server.await
        };

        Ok((server_fut, RepositoryServer { local_addr, stop }))
    }
}

async fn handle_request(
    repo_manager: Arc<RepositoryManager>,
    req: Request<Body>,
    local_addr: SocketAddr,
) -> Response<Body> {
    let mut path = req.uri().path();

    // Ignore the leading slash.
    if path.starts_with('/') {
        path = &path[1..];
    }

    let mut parts = path.splitn(2, '/');
    let repo_name = parts.next().expect("split should produce at least 1 item");

    if repo_name.is_empty() {
        // FIXME: consider returning a list of repositories to make it easier for users to browse
        // the repository server.
        return status_response(StatusCode::NOT_FOUND);
    }

    let resource_path = if let Some(resource_path) = parts.next() {
        resource_path
    } else {
        // FIXME: consider returning a list of repositories to make it easier for users to browse
        // the repository server.
        return status_response(StatusCode::NOT_FOUND);
    };

    if resource_path.is_empty() {
        // FIXME: consider returning a list of repositories to make it easier for users to browse
        // the repository server.
        return status_response(StatusCode::NOT_FOUND);
    }

    let repo = if let Some(repo) = repo_manager.get(repo_name) {
        repo
    } else {
        warn!("could not find repository {}", repo_name);
        return status_response(StatusCode::NOT_FOUND);
    };

    let resource = match resource_path {
        "repo.json" => {
            let config = match repo.get_config(&local_addr.to_string()).await {
                Ok(c) => c,
                Err(e) => {
                    error!("failed to generate config: {:?}", e);
                    return status_response(StatusCode::INTERNAL_SERVER_ERROR);
                }
            };
            match config.try_into() {
                Ok(c) => c,
                Err(e) => {
                    error!("failed to generate config: {:?}", e);
                    return status_response(StatusCode::INTERNAL_SERVER_ERROR);
                }
            }
        }
        _ => match repo.fetch(resource_path).await {
            Ok(file) => file,
            Err(Error::NotFound) => {
                warn!("could not find resource: {}", resource_path);
                return status_response(StatusCode::NOT_FOUND);
            }
            Err(Error::InvalidPath(path)) => {
                warn!("invalid path: {}", path.display());
                return status_response(StatusCode::BAD_REQUEST);
            }
            Err(err) => {
                error!("error fetching file {}: {:?}", resource_path, err);
                return status_response(StatusCode::INTERNAL_SERVER_ERROR);
            }
        },
    };

    Response::builder()
        .status(200)
        .header("Content-Length", resource.len)
        .body(Body::wrap_stream(resource.stream))
        .unwrap()
}

fn status_response(status_code: StatusCode) -> Response<Body> {
    Response::builder().status(status_code).body(Body::empty()).unwrap()
}

/// Adapt [async-net::TcpStream] to work with hyper.
struct HyperStream(TcpStream);

impl tokio::io::AsyncRead for HyperStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.0).poll_read(cx, buf)
    }
}

impl tokio::io::AsyncWrite for HyperStream {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.0).poll_write(cx, buf)
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.0).poll_flush(cx)
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.0).poll_close(cx)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::repository::{
            FileSystemRepository, MirrorConfig, Repository, RepositoryConfig, RepositoryKeyConfig,
            RepositoryManager, RepositoryMetadata,
        },
        anyhow::Result,
        bytes::Bytes,
        fuchsia_async as fasync,
        matches::assert_matches,
        std::{
            fs::{create_dir, File},
            io::Write as _,
            net::Ipv4Addr,
            path::Path,
        },
    };

    async fn get(url: impl AsRef<str>) -> Result<Response<Body>> {
        let req = Request::get(url.as_ref()).body(Body::empty())?;
        let client = fuchsia_hyper::new_client();
        let response = client.request(req).await?;
        Ok(response)
    }

    async fn get_bytes(url: impl AsRef<str>) -> Result<Bytes> {
        let response = get(url).await?;
        assert_eq!(response.status(), StatusCode::OK);
        Ok(hyper::body::to_bytes(response).await?)
    }

    fn write_file(path: &Path, body: &[u8]) {
        let mut f = File::create(path).unwrap();
        f.write(body).unwrap();
    }

    async fn verify_repo_json(devhost: &str, server_url: &str, keys: Vec<RepositoryKeyConfig>) {
        let url = format!("{}/{}/repo.json", server_url, devhost);
        let json: RepositoryConfig =
            serde_json::from_slice(&get_bytes(&url).await.unwrap()).unwrap();

        let expected = RepositoryConfig {
            repo_url: Some(format!("fuchsia-pkg://{}", devhost)),
            root_keys: Some(keys),
            root_version: Some(1),
            mirrors: Some(vec![MirrorConfig {
                mirror_url: Some(server_url.to_string()),
                subscribe: Some(false),
            }]),
        };

        assert_eq!(json, expected);
    }

    async fn run_test<F, R>(manager: Arc<RepositoryManager>, test: F)
    where
        F: Fn(String) -> R,
        R: Future<Output = ()>,
    {
        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, server) =
            RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

        // Run the server in the background.
        let task = fasync::Task::local(server_fut);

        test(server.local_url()).await;

        // Signal the server to shutdown.
        server.stop();

        // Wait for the server to actually shut down.
        task.await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_stop() {
        let manager = RepositoryManager::new();
        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, server) =
            RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

        // Run the server in the background.
        let task = fasync::Task::local(server_fut);

        // Signal the server to shutdown.
        server.stop();

        // Wait for the server to actually shut down.
        task.await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_empty_server_404s() {
        let manager = RepositoryManager::new();

        run_test(manager, |server_url| async move {
            let result = get(server_url).await.unwrap();
            assert_eq!(result.status(), StatusCode::NOT_FOUND);
        })
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_files_from_repositories() {
        let manager = RepositoryManager::new();

        let d = tempfile::tempdir().unwrap();

        let test_cases = [
            ("devhost-0", ["0-0", "0-1"], &vec![RepositoryKeyConfig::Ed25519Key(vec![1, 2, 3, 4])]),
            ("devhost-1", ["1-0", "1-1"], &vec![RepositoryKeyConfig::Ed25519Key(vec![5, 6, 7, 8])]),
        ];

        for (devhost, bodies, keys) in &test_cases {
            let dir = d.path().join(devhost);
            create_dir(&dir).unwrap();

            for body in &bodies[..] {
                write_file(&dir.join(body), body.as_bytes());
            }

            let repo = Repository::new_with_metadata(
                devhost,
                Box::new(FileSystemRepository::new(dir)),
                RepositoryMetadata::new(keys.to_vec(), 1),
            );
            manager.add(Arc::new(repo));
        }

        run_test(manager, |server_url| async move {
            for (devhost, bodies, key_vec) in &test_cases {
                for body in &bodies[..] {
                    let url = format!("{}/{}/{}", server_url, devhost, body);
                    assert_matches!(get_bytes(&url).await, Ok(bytes) if bytes == &body[..]);
                }
                verify_repo_json(devhost, &server_url, key_vec.to_vec()).await;
            }
        })
        .await
    }
}
