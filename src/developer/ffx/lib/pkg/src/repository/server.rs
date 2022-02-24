// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{Error, Repository, RepositoryId, RepositoryManager, Resource, ResourceRange},
    anyhow::Result,
    async_net::{TcpListener, TcpStream},
    chrono::Utc,
    fuchsia_async as fasync,
    futures::{prelude::*, AsyncRead, AsyncWrite, TryStreamExt},
    http_sse::{Event, EventSender, SseResponseCreator},
    hyper::{
        body::Body,
        header::RANGE,
        server::{accept::from_stream, Server},
        service::{make_service_fn, service_fn},
        HeaderMap, Request, Response, StatusCode,
    },
    log::{error, info, warn},
    parking_lot::RwLock,
    serde::{Deserialize, Serialize},
    std::{
        collections::{hash_map::Entry, HashMap},
        convert::Infallible,
        future::Future,
        io,
        net::SocketAddr,
        pin::Pin,
        sync::{Arc, Weak},
        task::{Context, Poll},
        time::Duration,
    },
};

// FIXME: This value was chosen basically at random.
const AUTO_BUFFER_SIZE: usize = 10;

// FIXME: This value was chosen basically at random.
const MAX_PARSE_RETRIES: usize = 5000;

// FIXME: This value was chosen basically at random.
const PARSE_RETRY_DELAY: Duration = Duration::from_micros(100);

type SseResponseCreatorMap = RwLock<HashMap<RepositoryId, Arc<SseResponseCreator>>>;

/// RepositoryManager represents the web server that serves [Repositories](Repository) to a target.
#[derive(Debug)]
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
    pub async fn start(
        self,
    ) -> Result<(
        impl Future<Output = ()>,
        futures::channel::mpsc::UnboundedSender<Result<ConnectionStream>>,
        RepositoryServer,
    )> {
        let listener = TcpListener::bind(&self.addr).await?;
        let local_addr = listener.local_addr()?;
        let sse_response_creators = Arc::new(RwLock::new(HashMap::new()));

        let repo_manager = Arc::clone(&self.repo_manager);
        let make_svc = make_service_fn(move |_socket| {
            // Each connection to the server receives a separate service_fn
            // instance, and so needs its own copy of the repository manager.
            let repo_manager = Arc::clone(&repo_manager);
            let sse_response_creators = Arc::clone(&sse_response_creators);

            async move {
                Ok::<_, Infallible>(service_fn(move |req| {
                    // Each request made by a connection is serviced by the
                    // service_fn created from this scope, which is why there is
                    // another cloning of the repository manager.
                    let method = req.method().to_string();
                    let path = req.uri().path().to_string();

                    handle_request(
                        Arc::clone(&repo_manager),
                        req,
                        Arc::clone(&sse_response_creators),
                    )
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
        let (sender, receiver) = futures::channel::mpsc::unbounded();
        let mut sender_server = sender.clone();

        // `listener.incoming()` borrows the listener, so we need to create a unique future that
        // owns the listener, and run the server in it.
        let server_fut = async move {
            // Shut down the incoming stream when the server future is dropped.
            let _incoming_fut = fuchsia_async::Task::spawn(async move {
                sender_server
                    .send_all(
                        &mut listener
                            .incoming()
                            .map_ok(ConnectionStream::Tcp)
                            .map_err(Into::into)
                            .map(Ok),
                    )
                    .await
            });

            let server = Server::builder(from_stream(receiver))
                .executor(fuchsia_hyper::LocalExecutor)
                .serve(make_svc)
                .with_graceful_shutdown(
                    rx_stop.map(|res| res.unwrap_or_else(|futures::channel::oneshot::Canceled| ())),
                )
                .unwrap_or_else(|e| panic!("unable to start server: {}", e));

            server.await
        };

        Ok((server_fut, sender, RepositoryServer { local_addr, stop }))
    }
}

async fn handle_request(
    repo_manager: Arc<RepositoryManager>,
    req: Request<Body>,
    sse_response_creators: Arc<SseResponseCreatorMap>,
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
    let headers = req.headers();
    let range = extract_range_from_range_header(headers);
    let range = if let Ok(range) = range {
        range
    } else {
        return status_response(StatusCode::RANGE_NOT_SATISFIABLE);
    };

    let resource = match resource_path {
        "auto" => {
            if repo.supports_watch() {
                return handle_auto(repo, sse_response_creators).await;
            } else {
                // The repo doesn't support watching.
                return status_response(StatusCode::NOT_FOUND);
            }
        }
        _ => {
            let res = if let Some(resource_path) = resource_path.strip_prefix("blobs/") {
                repo.fetch_blob_range(resource_path, range.clone()).await
            } else {
                repo.fetch_metadata_range(resource_path, range.clone()).await
            };

            match res {
                Ok(file) => file,
                Err(Error::NotFound) => {
                    warn!("could not find resource: {}", resource_path);
                    return status_response(StatusCode::NOT_FOUND);
                }
                Err(Error::InvalidPath(path)) => {
                    warn!("invalid path: {}", path);
                    return status_response(StatusCode::BAD_REQUEST);
                }
                Err(Error::RangeNotSatisfiable) => {
                    warn!("invalid range: {:?}", range);
                    return status_response(StatusCode::RANGE_NOT_SATISFIABLE);
                }
                Err(err) => {
                    error!("error fetching file {}: {:?}", resource_path, err);
                    return status_response(StatusCode::INTERNAL_SERVER_ERROR);
                }
            }
        }
    };

    generate_response_from_range(range, resource)
}

fn generate_response_from_range(range: ResourceRange, resource: Resource) -> Response<Body> {
    match range {
        ResourceRange::RangeFull => Response::builder()
            .status(200)
            .header("Content-Length", resource.content_len)
            .header("Accept-Ranges", "bytes")
            .body(Body::wrap_stream(resource.stream))
            .unwrap(),
        ResourceRange::RangeFrom { start } => Response::builder()
            .status(206)
            .header("Content-Length", resource.content_len - start)
            .header("Content-Range", format!("bytes={}-/{}", start, resource.total_len))
            .body(Body::wrap_stream(resource.stream))
            .unwrap(),
        ResourceRange::Range { start, end } => Response::builder()
            .status(206)
            .header("Content-Length", end - start)
            .header("Content-Range", format!("bytes={}-{}/{}", start, end, resource.total_len))
            .body(Body::wrap_stream(resource.stream))
            .unwrap(),
        ResourceRange::RangeTo { end } => Response::builder()
            .status(206)
            .header("Content-Length", end)
            .header("Content-Range", format!("bytes=-{}/{}", end, resource.total_len))
            .body(Body::wrap_stream(resource.stream))
            .unwrap(),
    }
}

fn extract_range_from_range_header(headers: &HeaderMap) -> Result<ResourceRange, Error> {
    if !headers.contains_key(RANGE) {
        return Ok(ResourceRange::RangeFull);
    }
    let range = headers[RANGE].to_str()?;
    let mut range_split = range.split("=");
    if range_split.next() != Some("bytes") {
        return Err(Error::RangeNotSatisfiable);
    }
    let mut vec = range_split.next().ok_or(Error::RangeNotSatisfiable)?.split("-");
    let start_str = vec.next().ok_or(Error::RangeNotSatisfiable)?;
    let end_str = vec.next().ok_or(Error::RangeNotSatisfiable)?;
    match (start_str.is_empty(), end_str.is_empty()) {
        (true, true) => Ok(ResourceRange::RangeFrom { start: 0 }),
        (false, false) => Ok(ResourceRange::Range {
            start: start_str.parse::<u64>()?,
            end: end_str.parse::<u64>()?,
        }),
        (false, true) => Ok(ResourceRange::RangeFrom { start: start_str.parse::<u64>()? }),
        (true, false) => Ok(ResourceRange::RangeTo { end: end_str.parse::<u64>()? }),
    }
}

async fn handle_auto(
    repo: Arc<Repository>,
    sse_response_creators: Arc<SseResponseCreatorMap>,
) -> Response<Body> {
    let id = repo.id();
    let response_creator = sse_response_creators.read().get(&id).map(Arc::clone);

    // Exit early if we've already created an auto-handler.
    if let Some(response_creator) = response_creator {
        return response_creator.create().await;
    }

    // Otherwise, create a timestamp watch stream. We'll do it racily to avoid holding the lock and
    // blocking the executor.
    let watcher = match repo.watch() {
        Ok(watcher) => watcher,
        Err(err) => {
            warn!("error creating file watcher: {}", err);
            return status_response(StatusCode::INTERNAL_SERVER_ERROR);
        }
    };

    // Next, create a response creator. It's possible we raced another call, which could have
    // already created a creator for us. This is denoted by `sender` being `None`.
    let (response_creator, sender) = match sse_response_creators.write().entry(id) {
        Entry::Occupied(entry) => (Arc::clone(entry.get()), None),
        Entry::Vacant(entry) => {
            // Next, create a response creator.
            let (response_creator, sender) =
                SseResponseCreator::with_additional_buffer_size(AUTO_BUFFER_SIZE);

            let response_creator = Arc::new(response_creator);
            entry.insert(Arc::clone(&response_creator));

            (response_creator, Some(sender))
        }
    };

    // Spawn the watcher if one doesn't exist already. This will run in the background, and register
    // a drop callback that will shut down the watcher when the repository is closed.
    if let Some(sender) = sender {
        let task = fasync::Task::local(timestamp_watcher(Arc::downgrade(&repo), sender, watcher));

        // Make sure the entry is cleaned up if the repository is deleted.
        let weak_sse_response_creators = Arc::downgrade(&sse_response_creators);
        repo.on_drop(move || {
            if let Some(sse_response_creators) = weak_sse_response_creators.upgrade() {
                sse_response_creators.write().remove(&id);
            }

            // shut down the task.
            drop(task);
        });
    };

    // Finally, create the response for the client.
    response_creator.create().await
}

#[derive(Serialize, Deserialize)]
struct SignedTimestamp {
    signed: TimestampFile,
}
#[derive(Serialize, Deserialize)]
struct TimestampFile {
    version: u32,
}

async fn timestamp_watcher<S>(repo: Weak<Repository>, sender: EventSender, mut watcher: S)
where
    S: Stream<Item = ()> + Unpin,
{
    let mut old_version = None;

    loop {
        // Temporarily upgrade the repository while we look up the timestamp.json's version.
        let version = match repo.upgrade() {
            Some(repo) => read_timestamp_version(repo).await,
            None => {
                // Exit our watcher if the repository has been deleted.
                return;
            }
        };

        if let Some(version) = version {
            if old_version != Some(version) {
                old_version = Some(version);

                sender
                    .send(
                        &Event::from_type_and_data("timestamp.json", version.to_string())
                            .expect("Could not assemble timestamp event"),
                    )
                    .await;
            }
        }

        // Exit the loop if the notify watcher has shut down.
        if watcher.next().await.is_none() {
            break;
        }
    }
}

// Try to read the timestamp.json's version from the repository, or return `None` if we experience
// any errors.
async fn read_timestamp_version(repo: Arc<Repository>) -> Option<u32> {
    for _ in 0..MAX_PARSE_RETRIES {
        // Read the timestamp file.
        //
        // FIXME: We should be using the TUF client to get the latest
        // timestamp in order to make sure the metadata is valid.
        match repo.fetch_metadata("timestamp.json").await {
            Ok(mut file) => {
                let mut bytes = vec![];
                match file.read_to_end(&mut bytes).await {
                    Ok(()) => match serde_json::from_slice::<SignedTimestamp>(&bytes) {
                        Ok(timestamp_file) => {
                            return Some(timestamp_file.signed.version);
                        }
                        Err(err) => {
                            warn!("failed to parse timestamp.json: {:#?}", err);
                        }
                    },
                    Err(err) => {
                        warn!("failed to read timestamp.json: {:#}", err);
                    }
                }
            }
            Err(err) => {
                warn!("failed to read timestamp.json: {:#?}", err);
            }
        };

        // We might see the file change when it's half-written, so we need to retry
        // the parse if it fails.
        fasync::Timer::new(PARSE_RETRY_DELAY).await;
    }

    // Failed to parse out the timestamp file.
    error!("failed to read timestamp.json after {} attempts", MAX_PARSE_RETRIES);

    None
}

fn status_response(status_code: StatusCode) -> Response<Body> {
    Response::builder().status(status_code).body(Body::empty()).unwrap()
}

/// Adapt [async-net::TcpStream] to work with hyper.
#[derive(Debug)]
pub enum ConnectionStream {
    Tcp(TcpStream),
    Socket(fasync::Socket),
}

impl tokio::io::AsyncRead for ConnectionStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_read(cx, buf),
            ConnectionStream::Socket(t) => Pin::new(t).poll_read(cx, buf),
        }
    }
}

impl tokio::io::AsyncWrite for ConnectionStream {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_write(cx, buf),
            ConnectionStream::Socket(t) => Pin::new(t).poll_write(cx, buf),
        }
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_flush(cx),
            ConnectionStream::Socket(t) => Pin::new(t).poll_flush(cx),
        }
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_close(cx),
            ConnectionStream::Socket(t) => Pin::new(t).poll_close(cx),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{repository::RepositoryManager, test_utils::make_writable_empty_repository},
        anyhow::Result,
        assert_matches::assert_matches,
        bytes::Bytes,
        camino::Utf8Path,
        fuchsia_async as fasync,
        http_sse::Client as SseClient,
        std::convert::TryInto,
        std::{fs::remove_file, io::Write as _, net::Ipv4Addr},
        timeout::timeout,
    };

    async fn get(url: impl AsRef<str>) -> Result<Response<Body>> {
        let req = Request::get(url.as_ref()).body(Body::empty())?;
        let client = fuchsia_hyper::new_client();
        let response = client.request(req).await?;
        Ok(response)
    }

    async fn get_bytes(url: impl AsRef<str> + std::fmt::Debug) -> Result<Bytes> {
        let response = get(url).await?;
        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(response.headers()["Accept-Ranges"], "bytes");
        Ok(hyper::body::to_bytes(response).await?)
    }

    async fn get_range(
        url: impl AsRef<str>,
        start: Option<u64>,
        end: Option<u64>,
    ) -> Result<Response<Body>> {
        let start_str = match start {
            Some(start) => start.to_string(),
            None => "".to_owned(),
        };
        let end_str = match end {
            Some(end) => end.to_string(),
            None => "".to_owned(),
        };
        let req = Request::get(url.as_ref())
            .header("Range", format!("bytes={}-{}", start_str, end_str))
            .body(Body::empty())?;
        let client = fuchsia_hyper::new_client();
        let response = client.request(req).await?;
        Ok(response)
    }

    async fn get_bytes_range(
        url: impl AsRef<str> + std::fmt::Debug,
        start: Option<u64>,
        end: Option<u64>,
        length: u64,
    ) -> Result<Bytes> {
        let response = get_range(url, start, end).await?;
        assert_eq!(response.status(), StatusCode::PARTIAL_CONTENT);
        let start_str = start.map(|i| i.to_string()).unwrap_or("".to_owned());
        let end_str = end.map(|i| i.to_string()).unwrap_or("".to_owned());

        let len = end.unwrap_or(length) - start.unwrap_or(0);
        assert_eq!(response.headers()["Content-Length"], len.to_string());
        assert_eq!(
            response.headers()["Content-Range"],
            format!("bytes={}-{}/{}", start_str, end_str, length.to_string())
        );
        Ok(hyper::body::to_bytes(response).await?)
    }

    fn write_file(path: &Utf8Path, body: &[u8]) {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write(body).unwrap();
        tmp.persist(path).unwrap();
    }

    async fn run_test<F, R>(manager: Arc<RepositoryManager>, test: F)
    where
        F: Fn(String) -> R,
        R: Future<Output = ()>,
    {
        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, _, server) =
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
        let (server_fut, _, server) =
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

        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let test_cases = [("devhost-0", ["0-0", "0-1"]), ("devhost-1", ["1-0", "1-1"])];

        for (devhost, bodies) in &test_cases {
            let dir = dir.join(devhost);
            let repo = make_writable_empty_repository(*devhost, dir.clone()).await.unwrap();

            for body in &bodies[..] {
                write_file(&dir.join("repository").join(body), body.as_bytes());
            }

            manager.add(Arc::new(repo));
        }

        run_test(manager, |server_url| async move {
            for (devhost, bodies) in &test_cases {
                for body in &bodies[..] {
                    let url = format!("{}/{}/{}", server_url, devhost, body);
                    assert_matches!(get_bytes(&url).await, Ok(bytes) if bytes == &body[..]);
                }
            }
        })
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_files_with_close_range_from_repositories() {
        let manager = RepositoryManager::new();

        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let test_cases = [("devhost-0", ["0-0", "0-1"]), ("devhost-1", ["1-0", "1-1"])];

        for (devhost, bodies) in &test_cases {
            let dir = dir.join(devhost);
            let repo = make_writable_empty_repository(*devhost, dir.clone()).await.unwrap();

            for body in &bodies[..] {
                write_file(&dir.join("repository").join(body), body.as_bytes());
            }

            manager.add(Arc::new(repo));
        }

        run_test(manager, |server_url| async move {
            for (devhost, bodies) in &test_cases {
                for body in &bodies[..] {
                    let url = format!("{}/{}/{}", server_url, devhost, body);
                    assert_matches!(get_bytes_range(&url, Some(1), Some(2), body.chars().count().try_into().unwrap()).await, Ok(bytes) if bytes == &body[1..2]);
                }
            }
        })
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_files_with_get_416_when_range_too_big() {
        let manager = RepositoryManager::new();

        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let test_cases = [("devhost-0", ["0-0", "0-1"]), ("devhost-1", ["1-0", "1-1"])];

        for (devhost, bodies) in &test_cases {
            let dir = dir.join(devhost);
            let repo = make_writable_empty_repository(*devhost, dir.clone()).await.unwrap();

            for body in &bodies[..] {
                write_file(&dir.join("repository").join(body), body.as_bytes());
            }

            manager.add(Arc::new(repo));
        }

        run_test(manager, |server_url| async move {
            for (devhost, bodies) in &test_cases {
                for body in &bodies[..] {
                    let url = format!("{}/{}/{}", server_url, devhost, body);
                    let response = get_range(&url, Some(1), Some(5)).await.unwrap();
                    assert_eq!(response.status(), StatusCode::RANGE_NOT_SATISFIABLE);
                }
            }
        })
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_auto_inner() {
        let manager = RepositoryManager::new();

        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap().join("devhost");

        let repo = make_writable_empty_repository("devhost", dir.clone()).await.unwrap();

        let timestamp_file = dir.join("repository").join("timestamp.json");
        write_file(
            &timestamp_file,
            serde_json::to_string(&SignedTimestamp { signed: TimestampFile { version: 1 } })
                .unwrap()
                .as_bytes(),
        );

        manager.add(Arc::new(repo));

        run_test(manager, |server_url| {
            let timestamp_file = timestamp_file.clone();
            async move {
                let url = format!("{}/devhost/auto", server_url);
                let mut client =
                    SseClient::connect(fuchsia_hyper::new_https_client(), url).await.unwrap();

                assert_eq!(
                    timeout(std::time::Duration::from_secs(3), client.next())
                        .await
                        .unwrap()
                        .unwrap()
                        .unwrap()
                        .data(),
                    "1"
                );
                write_file(
                    &timestamp_file,
                    serde_json::to_string(&SignedTimestamp {
                        signed: TimestampFile { version: 2 },
                    })
                    .unwrap()
                    .as_bytes(),
                );
                assert_eq!(client.next().await.unwrap().unwrap().data(), "2");

                write_file(
                    &timestamp_file,
                    serde_json::to_string(&SignedTimestamp {
                        signed: TimestampFile { version: 3 },
                    })
                    .unwrap()
                    .as_bytes(),
                );
                assert_eq!(client.next().await.unwrap().unwrap().data(), "3");

                remove_file(&timestamp_file).unwrap();
                write_file(
                    &timestamp_file,
                    serde_json::to_string(&SignedTimestamp {
                        signed: TimestampFile { version: 4 },
                    })
                    .unwrap()
                    .as_bytes(),
                );
                assert_eq!(client.next().await.unwrap().unwrap().data(), "4");
            }
        })
        .await;

        // FIXME(https://github.com/notify-rs/notify/pull/337): On OSX, notify uses a
        // crossbeam-channel in `Drop` to shut down the interior thread. Unfortunately this can
        // trip over an issue where OSX will tear down the thread local storage before shutting
        // down the thread, which can trigger a panic. To avoid this issue, sleep a little bit
        // after shutting down our stream.
        fasync::Timer::new(Duration::from_millis(100)).await;
    }
}
