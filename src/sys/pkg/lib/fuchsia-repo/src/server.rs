// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        manager::RepositoryManager,
        range::Range,
        repo_client::RepoClient,
        repository::{Error, RepoProvider},
    },
    anyhow::Result,
    async_lock::RwLock as AsyncRwLock,
    async_net::{TcpListener, TcpStream},
    chrono::Utc,
    fuchsia_async as fasync,
    futures::{future::Shared, prelude::*, AsyncRead, AsyncWrite, TryStreamExt},
    http_sse::{Event, EventSender, SseResponseCreator},
    hyper::{body::Body, header::RANGE, service::service_fn, Request, Response, StatusCode},
    serde::{Deserialize, Serialize},
    std::{
        collections::HashMap,
        convert::Infallible,
        future::Future,
        io,
        net::SocketAddr,
        pin::Pin,
        sync::{Arc, Mutex, RwLock as SyncRwLock, Weak},
        task::{Context, Poll},
        time::Duration,
    },
    tracing::{error, info, warn},
};

// FIXME: This value was chosen basically at random.
const AUTO_BUFFER_SIZE: usize = 10;

// FIXME: This value was chosen basically at random.
const MAX_PARSE_RETRIES: usize = 5000;

// FIXME: This value was chosen basically at random.
const PARSE_RETRY_DELAY: Duration = Duration::from_micros(100);

type SseResponseCreatorMap = SyncRwLock<HashMap<String, Arc<SseResponseCreator>>>;

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

        let (tx_stop_server, rx_stop_server) = futures::channel::oneshot::channel();
        let (tx_conns, rx_conns) = futures::channel::mpsc::unbounded();

        let server_fut =
            run_server(listener, rx_conns, rx_stop_server, Arc::clone(&self.repo_manager));

        Ok((server_fut, tx_conns, RepositoryServer { local_addr, stop: tx_stop_server }))
    }
}

/// Executor to help run tasks in the background, but still allow these tasks to be cleaned up when
/// the server is shut down.
#[derive(Debug, Default, Clone)]
struct TaskExecutor<T: 'static> {
    inner: Arc<Mutex<TaskExecutorInner<T>>>,
}

#[derive(Debug, Default)]
struct TaskExecutorInner<T> {
    next_task_id: u64,
    tasks: HashMap<u64, fasync::Task<T>>,
}

impl<T> TaskExecutor<T> {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(TaskExecutorInner {
                next_task_id: 0,
                tasks: HashMap::new(),
            })),
        }
    }

    /// Spawn a task in the executor.
    pub fn spawn<F: Future<Output = T> + 'static>(&self, fut: F) {
        let fut_inner = Arc::clone(&self.inner);

        let mut inner = self.inner.lock().unwrap();

        // We could technically have a collision when the task id overflows, but if we spawned a
        // task once a nanosecond, we still wouldn't overflow for 584 years. But lets add an
        // assertion just to be safe.
        let task_id = inner.next_task_id;
        inner.next_task_id = inner.next_task_id.wrapping_add(1);
        assert!(!inner.tasks.contains_key(&task_id));

        let fut = async move {
            let res = fut.await;
            // Clean up our entry when the task completes.
            fut_inner.lock().unwrap().tasks.remove(&task_id);
            res
        };

        inner.tasks.insert(task_id, fasync::Task::local(fut));
    }
}

impl<T, F: Future<Output = T> + 'static> hyper::rt::Executor<F> for TaskExecutor<T> {
    fn execute(&self, fut: F) {
        self.spawn(fut);
    }
}

/// Starts the server loop.
async fn run_server(
    listener: TcpListener,
    tunnel_conns: futures::channel::mpsc::UnboundedReceiver<Result<ConnectionStream>>,
    server_stopped: futures::channel::oneshot::Receiver<()>,
    server_repo_manager: Arc<RepositoryManager>,
) {
    // Turn the shutdown signal into a shared future, so we can signal to the server to and all the
    // auto/ SSE processes, to initiate a graceful shutdown.
    let mut server_stopped = server_stopped.shared();

    // Merge the listener connections with the tunnel connections.
    let mut incoming = futures::stream::select(
        listener.incoming().map_ok(ConnectionStream::Tcp).map_err(Into::into),
        tunnel_conns,
    );

    // Spawn all connections and related tasks in this executor.
    let executor = TaskExecutor::new();

    // Contains all the SSE services.
    let server_sse_response_creators = Arc::new(SyncRwLock::new(HashMap::new()));

    loop {
        let mut next = incoming.next().fuse();
        let conn = futures::select! {
            conn = next => {
                match conn {
                    Some(Ok(conn)) => conn,
                    Some(Err(err)) => {
                        error!("failed to accept connection: {:?}", err);
                        continue;
                    }
                    None => {
                        unreachable!(
                            "incoming stream has ended, which should be impossible \
                            according to async_net::TcpListener logs"
                        );
                    }
                }
            },
            _ = server_stopped => {
                break;
            },
        };

        let service_rx_stop = server_stopped.clone();
        let service_repo_manager = Arc::clone(&server_repo_manager);
        let service_sse_response_creators = Arc::clone(&server_sse_response_creators);

        executor.spawn(handle_connection(
            executor.clone(),
            service_rx_stop,
            service_repo_manager,
            service_sse_response_creators,
            conn,
        ));
    }
}

async fn handle_connection(
    executor: TaskExecutor<()>,
    server_stopped: Shared<futures::channel::oneshot::Receiver<()>>,
    repo_manager: Arc<RepositoryManager>,
    sse_response_creators: Arc<SseResponseCreatorMap>,
    conn: ConnectionStream,
) {
    let service_server_stopped = server_stopped.clone();
    let conn = hyper::server::conn::Http::new().with_executor(executor.clone()).serve_connection(
        conn,
        service_fn(|req| {
            // Each request made by a connection is serviced by the
            // service_fn created from this scope, which is why there is
            // another cloning of the repository manager.
            let method = req.method().to_string();
            let path = req.uri().path().to_string();

            handle_request(
                executor.clone(),
                service_server_stopped.clone(),
                Arc::clone(&repo_manager),
                Arc::clone(&sse_response_creators),
                req,
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
        }),
    );

    let conn = GracefulConnection { conn, stop: server_stopped, shutting_down: false };

    if let Err(err) = conn.await {
        error!("Error while serving HTTP connection: {}", err);
    }
}

/// [GracefulConnection] will signal to the connection to shut down if we receive a shutdown signal
/// on the `stop` channel.
#[pin_project::pin_project]
struct GracefulConnection<S>
where
    S: hyper::service::Service<Request<Body>, Response = Response<Body>>,
    S::Error: std::error::Error + Send + Sync + 'static,
    S::Future: 'static,
{
    #[pin]
    stop: Shared<futures::channel::oneshot::Receiver<()>>,

    /// The hyper connection.
    #[pin]
    conn: hyper::server::conn::Connection<ConnectionStream, S, TaskExecutor<()>>,

    shutting_down: bool,
}

impl<S> Future for GracefulConnection<S>
where
    S: hyper::service::Service<Request<Body>, Response = Response<Body>>,
    S::Error: std::error::Error + Send + Sync + 'static,
    S::Future: 'static,
{
    type Output = Result<(), hyper::Error>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut this = self.project();
        if !*this.shutting_down {
            match this.stop.poll(cx) {
                Poll::Pending => {}
                Poll::Ready(_) => {
                    *this.shutting_down = true;

                    // Tell the connection to begin to gracefully shut down the connection.
                    // According to the [docs], we need to continue polling the connection until
                    // completion. This allows hyper to flush any send queues, or emit any
                    // HTTP-level shutdown messages. That would help the client to distinguish
                    // the server going away on purpose, or some other unexpected error.
                    //
                    // [docs]: https://docs.rs/hyper/latest/hyper/server/conn/struct.Connection.html#method.graceful_shutdown
                    this.conn.as_mut().graceful_shutdown();
                }
            }
        }

        match this.conn.poll(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(x) => Poll::Ready(x),
        }
    }
}

async fn handle_request(
    executor: TaskExecutor<()>,
    server_stopped: Shared<futures::channel::oneshot::Receiver<()>>,
    repo_manager: Arc<RepositoryManager>,
    sse_response_creators: Arc<SseResponseCreatorMap>,
    req: Request<Body>,
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
    let range = if let Some(header) = headers.get(RANGE) {
        if let Ok(range) = Range::from_http_range_header(header) {
            range
        } else {
            return status_response(StatusCode::RANGE_NOT_SATISFIABLE);
        }
    } else {
        Range::Full
    };

    let resource = match resource_path {
        "auto" => {
            if repo.read().await.supports_watch() {
                return handle_auto(
                    executor,
                    server_stopped,
                    repo_name,
                    repo,
                    sse_response_creators,
                )
                .await;
            } else {
                // The repo doesn't support watching.
                return status_response(StatusCode::NOT_FOUND);
            }
        }
        _ => {
            let res = if let Some(resource_path) = resource_path.strip_prefix("blobs/") {
                repo.read().await.fetch_blob_range(resource_path, range).await
            } else {
                repo.read().await.fetch_metadata_range(resource_path, range).await
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

    // Send the response back to the caller.
    let mut builder = Response::builder()
        .header("Accept-Ranges", "bytes")
        .header("Content-Length", resource.content_len());

    // If we requested a subset of the file, respond with the partial content headers.
    builder = if let Some(content_range) = resource.content_range.to_http_content_range_header() {
        builder.header("Content-Range", content_range).status(StatusCode::PARTIAL_CONTENT)
    } else {
        builder.status(StatusCode::OK)
    };

    builder.body(Body::wrap_stream(resource.stream)).unwrap()
}

async fn handle_auto(
    executor: TaskExecutor<()>,
    mut server_stopped: Shared<futures::channel::oneshot::Receiver<()>>,
    repo_name: &str,
    repo: Arc<AsyncRwLock<RepoClient<Box<dyn RepoProvider>>>>,
    sse_response_creators: Arc<SseResponseCreatorMap>,
) -> Response<Body> {
    let response_creator = sse_response_creators.read().unwrap().get(repo_name).map(Arc::clone);

    // Exit early if we've already created an auto-handler.
    if let Some(response_creator) = response_creator {
        return response_creator.create().await;
    }

    // Otherwise, create a timestamp watch stream. We'll do it racily to avoid holding the lock and
    // blocking the executor.
    let watcher = match repo.read().await.watch() {
        Ok(watcher) => watcher,
        Err(err) => {
            warn!("error creating file watcher: {}", err);
            return status_response(StatusCode::INTERNAL_SERVER_ERROR);
        }
    };

    // Next, create a response creator. It's possible we raced another call, which could have
    // already created a creator for us. This is denoted by `sender` being `None`.
    let (response_creator, sender) = {
        let mut sse_response_creators = sse_response_creators.write().unwrap();

        if let Some(response_creator) = sse_response_creators.get(repo_name) {
            (Arc::clone(response_creator), None)
        } else {
            // Next, create a response creator.
            let (response_creator, sender) =
                SseResponseCreator::with_additional_buffer_size(AUTO_BUFFER_SIZE);

            let response_creator = Arc::new(response_creator);
            sse_response_creators.insert(repo_name.to_owned(), Arc::clone(&response_creator));

            (response_creator, Some(sender))
        }
    };

    // Spawn the watcher if one doesn't exist already. This will run in the background, and register
    // a drop callback that will shut down the watcher when the repository is closed.
    if let Some(sender) = sender {
        // Make sure the entry is cleaned up if the repository is deleted.
        let sse_response_creators = Arc::downgrade(&sse_response_creators);

        // Grab a handle to the repo dropped signal, so we can shut down our watcher.
        let mut repo_dropped = repo.read().await.on_dropped_signal();

        // Downgrade our repository handle, so we won't block it being deleted.
        let repo_name = repo_name.to_owned();
        let repo = Arc::downgrade(&repo);

        executor.spawn(async move {
            let watcher_fut = timestamp_watcher(repo, sender, watcher).fuse();
            futures::pin_mut!(watcher_fut);

            // Run the task until the watcher exits, or we were asked to cancel.
            futures::select! {
                () = watcher_fut => {},
                _ = server_stopped => {},
                _ = repo_dropped => (),
            };

            // Clean up our SSE creators.
            if let Some(sse_response_creators) = sse_response_creators.upgrade() {
                sse_response_creators.write().unwrap().remove(&repo_name);
            }
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

async fn timestamp_watcher<S>(
    repo: Weak<AsyncRwLock<RepoClient<Box<dyn RepoProvider>>>>,
    sender: EventSender,
    mut watcher: S,
) where
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
async fn read_timestamp_version(
    repo: Arc<AsyncRwLock<RepoClient<Box<dyn RepoProvider>>>>,
) -> Option<u32> {
    for _ in 0..MAX_PARSE_RETRIES {
        // Read the timestamp file.
        //
        // FIXME: We should be using the TUF client to get the latest
        // timestamp in order to make sure the metadata is valid.
        match repo.read().await.fetch_metadata("timestamp.json").await {
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

/// Adapt [async_net::TcpStream] to work with hyper.
#[derive(Debug)]
pub enum ConnectionStream {
    Tcp(TcpStream),
    Socket(fasync::Socket),
}

impl tokio::io::AsyncRead for ConnectionStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut tokio::io::ReadBuf<'_>,
    ) -> Poll<io::Result<()>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_read(cx, buf.initialize_unfilled()),
            ConnectionStream::Socket(t) => Pin::new(t).poll_read(cx, buf.initialize_unfilled()),
        }
        .map_ok(|sz| {
            buf.advance(sz);
        })
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
        crate::{manager::RepositoryManager, test_utils::make_writable_empty_repository},
        anyhow::Result,
        assert_matches::assert_matches,
        bytes::Bytes,
        camino::Utf8Path,
        fuchsia_async as fasync,
        http_sse::Client as SseClient,
        std::convert::TryInto,
        std::{fs::remove_file, io::Write as _, net::Ipv4Addr},
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
        total_len: u64,
    ) -> Result<Bytes> {
        let response = get_range(url, start, end).await?;
        assert_eq!(response.status(), StatusCode::PARTIAL_CONTENT);

        // http ranges are inclusive, so we need to add one to `end` to compute the content length.
        let content_len = end.map(|end| end + 1).unwrap_or(total_len) - start.unwrap_or(0);
        assert_eq!(response.headers()["Content-Length"], content_len.to_string());

        let start_str = start.map(|i| i.to_string()).unwrap_or_else(String::new);
        let end_str = end.map(|i| i.to_string()).unwrap_or_else(String::new);
        assert_eq!(
            response.headers()["Content-Range"],
            format!("bytes {}-{}/{}", start_str, end_str, total_len)
        );

        Ok(hyper::body::to_bytes(response).await?)
    }

    fn write_file(path: &Utf8Path, body: &[u8]) {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write_all(body).unwrap();
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
            let repo = make_writable_empty_repository(dir.clone()).await.unwrap();

            for body in &bodies[..] {
                write_file(&dir.join("repository").join(body), body.as_bytes());
            }

            manager.add(*devhost, repo);
        }

        run_test(manager, |server_url| async move {
            for (devhost, bodies) in &test_cases {
                for body in &bodies[..] {
                    let url = format!("{}/{}/{}", server_url, devhost, body);
                    assert_matches!(get_bytes(&url).await, Ok(bytes) if bytes == body[..]);
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
            let repo = make_writable_empty_repository(dir.clone()).await.unwrap();

            for body in &bodies[..] {
                write_file(&dir.join("repository").join(body), body.as_bytes());
            }

            manager.add(*devhost, repo);
        }

        run_test(manager, |server_url| async move {
            for (devhost, bodies) in &test_cases {
                for body in &bodies[..] {
                    let url = format!("{}/{}/{}", server_url, devhost, body);

                    assert_eq!(
                        &body[1..=2],
                        get_bytes_range(
                            &url,
                            Some(1),
                            Some(2),
                            body.chars().count().try_into().unwrap(),
                        )
                        .await
                        .unwrap()
                    );
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
            let repo = make_writable_empty_repository(dir.clone()).await.unwrap();

            for body in &bodies[..] {
                write_file(&dir.join("repository").join(body), body.as_bytes());
            }

            manager.add(*devhost, repo);
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

        let repo = make_writable_empty_repository(dir.clone()).await.unwrap();

        let timestamp_file = dir.join("repository").join("timestamp.json");
        write_file(
            &timestamp_file,
            serde_json::to_string(&SignedTimestamp { signed: TimestampFile { version: 1 } })
                .unwrap()
                .as_bytes(),
        );

        manager.add("devhost", repo);

        run_test(manager, |server_url| {
            let timestamp_file = timestamp_file.clone();
            async move {
                let url = format!("{}/devhost/auto", server_url);
                let mut client =
                    SseClient::connect(fuchsia_hyper::new_https_client(), url).await.unwrap();

                futures::select! {
                    value = client.next().fuse() => {
                        assert_eq!(value.unwrap().unwrap().data(), "1");
                    },
                    () = fuchsia_async::Timer::new(Duration::from_secs(3)).fuse() => {
                        panic!("timed out reading auto");
                    },
                }

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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_shutdown_timeout() {
        let manager = RepositoryManager::new();

        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap().join("devhost");

        let repo = make_writable_empty_repository(dir.clone()).await.unwrap();

        let timestamp_file = dir.join("repository").join("timestamp.json");
        write_file(
            &timestamp_file,
            serde_json::to_string(&SignedTimestamp { signed: TimestampFile { version: 1 } })
                .unwrap()
                .as_bytes(),
        );

        manager.add("devhost", repo);

        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, _, server) =
            RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

        // Run the server in the background.
        let server_task = fasync::Task::local(server_fut);

        // Connect to the sse endpoint.
        let url = format!("{}/devhost/auto", server.local_url());

        // wait for an SSE event in the background.
        let (tx_sse_connected, rx_sse_connected) = futures::channel::oneshot::channel();
        let sse_task = fasync::Task::local(async move {
            let mut sse = SseClient::connect(fuchsia_hyper::new_https_client(), url).await.unwrap();

            // We should receive one event for the current timestamp.
            sse.next().await.unwrap().unwrap();

            tx_sse_connected.send(()).unwrap();

            // We should block until we receive an error because the server went away.
            match sse.next().await {
                Some(Ok(event)) => {
                    panic!("unexpected event {:?}", event);
                }
                Some(Err(_)) => {}
                None => {
                    panic!("unexpected channel close");
                }
            }
        });

        // wait for the sse client to connect to the server.
        rx_sse_connected.await.unwrap();

        // Signal the server to shutdown.
        server.stop();

        // The server should shutdown after the timeout period.
        server_task.await;

        futures::select! {
            () = sse_task.fuse() => {},

            () = fuchsia_async::Timer::new(Duration::from_secs(10)).fuse() => {
                panic!("sse task failed to shut down");
            },
        }
    }
}
