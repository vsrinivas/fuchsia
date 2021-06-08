// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! UriPathHandler implementations

use {
    crate::serve::{RangeUriPathHandler, UriPathHandler},
    futures::{
        channel::{mpsc, oneshot},
        future::{pending, ready, BoxFuture},
        prelude::*,
    },
    hyper::{header::CONTENT_LENGTH, Body, Response, StatusCode},
    parking_lot::Mutex,
    std::{
        collections::HashSet,
        path::{Path, PathBuf},
        sync::{
            atomic::{AtomicBool, AtomicU16, AtomicU32, Ordering},
            Arc,
        },
    },
};

/// Handler that always responds with the given status code
pub struct StaticResponseCode(StatusCode);

impl UriPathHandler for StaticResponseCode {
    fn handle(&self, _uri_path: &Path, _response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        ready(Response::builder().status(self.0).body(Body::empty()).unwrap()).boxed()
    }
}

impl StaticResponseCode {
    /// Creates handler that always responds with the given status code
    pub fn new(status: StatusCode) -> Self {
        Self(status)
    }

    /// Creates handler that always responds with 200 OK
    pub fn ok() -> Self {
        Self(StatusCode::OK)
    }

    /// Creates handler that always responds with 404 Not Found
    pub fn not_found() -> Self {
        Self(StatusCode::NOT_FOUND)
    }

    /// Creates handler that always responds with 500 Internal Server Error
    pub fn server_error() -> Self {
        Self(StatusCode::INTERNAL_SERVER_ERROR)
    }

    /// Creates handler that always responds with 429 Too Many Requests
    pub fn too_many_requests() -> Self {
        Self(StatusCode::TOO_MANY_REQUESTS)
    }
}

/// Handler that always responds with the given status code
pub struct RangeStaticResponseCode(StatusCode);

impl RangeUriPathHandler for RangeStaticResponseCode {
    fn handle(
        &self,
        _: &Path,
        _: &http::HeaderValue,
        _: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        ready(Response::builder().status(self.0).body(Body::empty()).unwrap()).boxed()
    }
}

impl RangeStaticResponseCode {
    /// Creates handler that always responds with 500 Internal Server Error
    pub fn server_error() -> Self {
        Self(StatusCode::INTERNAL_SERVER_ERROR)
    }
}

/// An atomic HTTP status code carrier.
#[derive(Debug, Default)]
pub struct DynamicResponseSetter(Arc<AtomicU16>);

impl DynamicResponseSetter {
    /// Atomically sets this toggle to the supplied code.
    pub fn set(&self, code: u16) {
        self.0.store(code, Ordering::SeqCst);
    }
}

/// Handler that replies with an externally-settable HTTP status.
pub struct DynamicResponseCode {
    code: Arc<AtomicU16>,
}

impl UriPathHandler for DynamicResponseCode {
    fn handle(&self, _uri_path: &Path, _response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        ready(
            Response::builder()
                .status(self.code.load(Ordering::SeqCst))
                .body(Body::empty())
                .unwrap(),
        )
        .boxed()
    }
}

impl DynamicResponseCode {
    /// Creates a new handler with a (re)settable status code.
    pub fn new(initial: u16) -> (Self, DynamicResponseSetter) {
        let setter = DynamicResponseSetter(Arc::new(initial.into()));
        (Self { code: Arc::clone(&setter.0) }, setter)
    }
}

/// An atomic toggle switch.
#[derive(Debug, Default)]
pub struct AtomicToggle(Arc<AtomicBool>);

impl AtomicToggle {
    /// Creates a new AtomicToggle initialized to `initial`.
    pub fn new(initial: bool) -> Self {
        Self(Arc::new(initial.into()))
    }

    /// Atomically sets this toggle to true.
    pub fn set(&self) {
        self.0.store(true, Ordering::SeqCst);
    }

    /// Atomically sets this toggle to false.
    pub fn unset(&self) {
        self.0.store(false, Ordering::SeqCst);
    }
}

/// Handler that overrides requests with the given handler only when enabled
pub struct Toggleable<H: UriPathHandler> {
    enabled: Arc<AtomicBool>,
    handler: H,
}

impl<H: UriPathHandler> UriPathHandler for Toggleable<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        if self.enabled.load(Ordering::SeqCst) {
            self.handler.handle(uri_path, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: UriPathHandler> Toggleable<H> {
    /// Creates handler that overrides requests when should_override is set.
    pub fn new(should_override: &AtomicToggle, handler: H) -> Self {
        Self { enabled: Arc::clone(&should_override.0), handler }
    }
}

/// Handler that overrides the given request path for the given number of requests.
pub struct ForRequestCount<H: UriPathHandler> {
    remaining: Mutex<u32>,
    handler: H,
}

impl<H: UriPathHandler> UriPathHandler for ForRequestCount<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        let mut remaining = self.remaining.lock();
        if *remaining > 0 {
            *remaining -= 1;
            drop(remaining);
            self.handler.handle(uri_path, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: UriPathHandler> ForRequestCount<H> {
    /// Creates handler that overrides the given request path for the given number of requests.
    pub fn new(count: u32, handler: H) -> Self {
        Self { remaining: Mutex::new(count), handler }
    }
}

/// Handler that overrides the given request path using the given handler.
pub struct ForPath<H: UriPathHandler> {
    path: PathBuf,
    handler: H,
}

impl<H: UriPathHandler> UriPathHandler for ForPath<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        if self.path == uri_path {
            self.handler.handle(uri_path, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: UriPathHandler> ForPath<H> {
    /// Creates handler that overrides the given request path using the given handler.
    pub fn new(path: impl Into<PathBuf>, handler: H) -> Self {
        Self { path: path.into(), handler }
    }
}

/// Handler that overrides the given request paths using the given handler.
pub struct ForPaths<H: UriPathHandler> {
    paths: HashSet<PathBuf>,
    handler: H,
}

impl<H: UriPathHandler> UriPathHandler for ForPaths<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        if self.paths.contains(uri_path) {
            self.handler.handle(uri_path, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: UriPathHandler> ForPaths<H> {
    /// Creates handler that overrides the given request paths using the given handler.
    pub fn new(paths: HashSet<PathBuf>, handler: H) -> Self {
        Self { paths, handler }
    }
}

/// Handler that overrides all the requests that start with the given request path using the
/// given handler.
pub struct ForPathPrefix<H: UriPathHandler> {
    prefix: PathBuf,
    handler: H,
}

impl<H: UriPathHandler> UriPathHandler for ForPathPrefix<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        if uri_path.starts_with(&self.prefix) {
            self.handler.handle(uri_path, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: UriPathHandler> ForPathPrefix<H> {
    /// Creates handler that overrides all the requests that start with the given request path
    /// using the given handler.
    pub fn new(prefix: impl Into<PathBuf>, handler: H) -> Self {
        Self { prefix: prefix.into(), handler }
    }
}

/// Handler that overrides all the requests that end with the given request path using the
/// given handler. Useful for hitting all versions of versioned TUF metadata (e.g. X.targets.json).
/// TODO(ampearce): change ForPathSuffix and ForPathPrefix to use string matches rather than path.
pub struct ForPathSuffix<H: UriPathHandler> {
    suffix: PathBuf,
    handler: H,
}

impl<H: UriPathHandler> UriPathHandler for ForPathSuffix<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        if uri_path.ends_with(&self.suffix) {
            self.handler.handle(uri_path, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: UriPathHandler> ForPathSuffix<H> {
    /// Creates handler that overrides all the requests that start with the given request path
    /// using the given handler.
    pub fn new(suffix: impl Into<PathBuf>, handler: H) -> Self {
        Self { suffix: suffix.into(), handler }
    }
}
/// Handler that passes responses through the given handler once per unique path.
pub struct OncePerPath<H: UriPathHandler> {
    handler: H,
    failed_paths: Mutex<HashSet<PathBuf>>,
}

impl<H: UriPathHandler> UriPathHandler for OncePerPath<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        if self.failed_paths.lock().insert(uri_path.to_owned()) {
            self.handler.handle(uri_path, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: UriPathHandler> OncePerPath<H> {
    /// Creates handler that passes responses through the given handler once per unique path.
    pub fn new(handler: H) -> Self {
        Self { handler, failed_paths: Mutex::new(HashSet::new()) }
    }
}

/// Transform a `serde_json::Value`. Implements `UriPathHandler` by assuming the `Response<Body>` is
/// json-formatted.
pub trait JsonTransformer: Send + Sync + Clone + 'static {
    /// Transform a `serde_json::Value`
    fn transform(&self, v: serde_json::Value) -> serde_json::Value;
}

impl<F> JsonTransformer for F
where
    F: Fn(serde_json::Value) -> serde_json::Value + Send + Sync + Clone + 'static,
{
    fn transform(&self, v: serde_json::Value) -> serde_json::Value {
        (self)(v)
    }
}

/// Handler that manipulates requests with json-formatted bodies.
impl<T: JsonTransformer> UriPathHandler for T {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        async move {
            let bytes = body_to_bytes(response.into_body()).await;
            let value = self.transform(serde_json::from_reader(bytes.as_slice()).unwrap());
            let bytes = serde_json::to_vec(&value).unwrap();
            Response::builder()
                .status(hyper::StatusCode::OK)
                .header(CONTENT_LENGTH, bytes.len())
                .body(Body::from(bytes))
                .expect("valid response")
        }
        .boxed()
    }
}

/// Handler that notifies a channel when it receives a request.
pub struct NotifyWhenRequested {
    notify: mpsc::UnboundedSender<()>,
}

impl NotifyWhenRequested {
    /// Creates a new handler and the receiver it notifies on request receipt.
    pub fn new() -> (Self, mpsc::UnboundedReceiver<()>) {
        let (tx, rx) = mpsc::unbounded();
        (Self { notify: tx }, rx)
    }
}

impl UriPathHandler for NotifyWhenRequested {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        self.notify.unbounded_send(()).unwrap();
        ready(response).boxed()
    }
}

/// A response that is waiting to be sent.
pub struct BlockedResponse {
    path: PathBuf,
    unblocker: oneshot::Sender<()>,
}

impl BlockedResponse {
    /// The path of the request.
    pub fn path(&self) -> &Path {
        &self.path
    }

    /// Send the response.
    pub fn unblock(self) {
        self.unblocker.send(()).expect("request to still be pending")
    }
}

/// Handler that blocks sending response headers and bodies until unblocked by a test.
pub struct BlockResponseHeaders {
    blocked_responses: mpsc::UnboundedSender<BlockedResponse>,
}

impl BlockResponseHeaders {
    /// Creates a new handler and the receiver it notifies on request receipt.
    pub fn new() -> (Self, mpsc::UnboundedReceiver<BlockedResponse>) {
        let (sender, receiver) = mpsc::unbounded();

        (Self { blocked_responses: sender }, receiver)
    }
}

impl UriPathHandler for BlockResponseHeaders {
    fn handle(&self, path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        // Return a future that notifies the test that the request was blocked and wait for it to
        // unblock the response.
        let path = path.to_owned();
        let mut blocked_responses = self.blocked_responses.clone();
        async move {
            let (unblocker, waiter) = oneshot::channel();
            blocked_responses
                .send(BlockedResponse { path, unblocker })
                .await
                .expect("receiver to still exist");
            waiter.await.expect("request to be unblocked");
            response
        }
        .boxed()
    }
}

/// Handler that blocks sending response bodies until unblocked by a test.
pub struct BlockResponseBodyOnce {
    notify: Mutex<Option<oneshot::Sender<Box<dyn FnOnce() + Send>>>>,
}

impl BlockResponseBodyOnce {
    /// Creates a new handler and the receiver it notifies after sending the response headers.
    pub fn new() -> (Self, oneshot::Receiver<Box<dyn FnOnce() + Send>>) {
        let (sender, receiver) = oneshot::channel();

        (Self { notify: Mutex::new(Some(sender)) }, receiver)
    }
}

impl UriPathHandler for BlockResponseBodyOnce {
    fn handle(&self, _path: &Path, mut response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        let notify = self.notify.lock().take().expect("a single request for this path");

        async move {
            // Replace the response's body with a stream that will yield data when the test
            // unblocks the response body.
            let (mut sender, new_body) = Body::channel();
            let old_body = std::mem::replace(response.body_mut(), new_body);
            let contents = body_to_bytes(old_body).await;

            // Notify the test.
            notify
                .send(Box::new(move || {
                    sender.try_send_data(contents.into()).expect("sending body")
                }))
                .map_err(|_| ())
                .expect("receiver to still exist");

            // Yield the modified response so hyper will send the headers and wait for the body to be
            // unblocked.
            response
        }
        .boxed()
    }
}

async fn body_to_bytes(body: Body) -> Vec<u8> {
    body.try_fold(Vec::new(), |mut vec, b| async move {
        vec.extend(b);
        Ok(vec)
    })
    .await
    .expect("body stream to complete")
}

/// Handler that yields the response up to the final byte, then produces an error.  Panics if the
/// response contains an empty body.
pub struct OneByteShortThenError;

impl UriPathHandler for OneByteShortThenError {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        async {
            let mut bytes = body_to_bytes(response.into_body()).await;
            if bytes.pop().is_none() {
                panic!("can't short 0 bytes");
            }
            Response::builder()
                .status(hyper::StatusCode::OK)
                .header(CONTENT_LENGTH, bytes.len() + 1)
                .body(Body::wrap_stream(futures::stream::iter(vec![
                    Ok(bytes),
                    Err("all_but_one_byte_then_eror has sent all but one bytes".to_string()),
                ])))
                .expect("valid response")
        }
        .boxed()
    }
}

/// Handler that yields the response up to the Nth byte, then produces an error.  Panics if the
/// response does not contain more than N bytes.
pub struct NBytesThenError {
    n: usize,
}

impl NBytesThenError {
    /// Make a handler that returns N bytes then errors.
    pub fn new(n: usize) -> Self {
        Self { n }
    }
}
impl UriPathHandler for NBytesThenError {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        let n = self.n;
        async move {
            let mut bytes = body_to_bytes(response.into_body()).await;
            let initial_len = bytes.len();
            if initial_len <= n {
                panic!("not enough bytes to shorten, {} {}", initial_len, n);
            }
            bytes.truncate(n);
            Response::builder()
                .status(hyper::StatusCode::OK)
                .header(CONTENT_LENGTH, initial_len)
                .body(Body::wrap_stream(futures::stream::iter(vec![
                    Ok(bytes),
                    Err("all_but_one_byte_then_eror has sent all but one bytes".to_string()),
                ])))
                .expect("valid response")
        }
        .boxed()
    }
}

/// Handler that yields the response up to the Nth byte, then produces an error.  Panics if the
/// response does not contain more than N bytes.
pub struct RangeNBytesThenError {
    n: usize,
}

impl RangeNBytesThenError {
    /// Make a handler that returns N bytes then errors.
    pub fn new(n: usize) -> Self {
        Self { n }
    }
}
impl RangeUriPathHandler for RangeNBytesThenError {
    fn handle(
        &self,
        _: &Path,
        _: &http::HeaderValue,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        let n = self.n;
        async move {
            let content_range_header =
                response.headers().get(http::header::CONTENT_RANGE).unwrap().clone();
            let mut bytes = body_to_bytes(response.into_body()).await;
            let initial_len = bytes.len();
            if initial_len <= n {
                panic!("not enough bytes to shorten, {} {}", initial_len, n);
            }
            bytes.truncate(n);
            Response::builder()
                .status(hyper::StatusCode::PARTIAL_CONTENT)
                .header(http::header::CONTENT_RANGE, content_range_header)
                .body(Body::wrap_stream(futures::stream::iter(vec![
                    Ok(bytes),
                    Err("all_but_one_byte_then_eror has sent all but one bytes".to_string()),
                ])))
                .expect("valid response")
        }
        .boxed()
    }
}

/// Handler that yields the response up to the final byte, then disconnects.  Panics if the
/// response contains an empty body.
pub struct OneByteShortThenDisconnect;

impl UriPathHandler for OneByteShortThenDisconnect {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        async {
            let mut bytes = body_to_bytes(response.into_body()).await;
            if bytes.pop().is_none() {
                panic!("can't short 0 bytes");
            }
            Response::builder()
                .status(hyper::StatusCode::OK)
                .header(CONTENT_LENGTH, bytes.len() + 1)
                .body(Body::wrap_stream(futures::stream::iter(vec![
                    Result::<Vec<u8>, String>::Ok(bytes),
                ])))
                .expect("valid response")
        }
        .boxed()
    }
}

/// Handler that flips the first byte of the response.  Panics if the response contains an empty
/// body.
pub struct OneByteFlipped;

impl UriPathHandler for OneByteFlipped {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        async {
            let mut bytes = body_to_bytes(response.into_body()).await;
            if bytes.is_empty() {
                panic!("can't flip 0 bytes");
            }
            bytes[0] = !bytes[0];
            Response::builder()
                .status(hyper::StatusCode::OK)
                .body(bytes.into())
                .expect("valid response")
        }
        .boxed()
    }
}

/// Handler that never sends bytes.
pub struct Hang;

impl UriPathHandler for Hang {
    fn handle(&self, _: &Path, _: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        pending().boxed()
    }
}

/// Handler that sends the header but then never sends body bytes.
pub struct HangBody;

impl UriPathHandler for HangBody {
    fn handle(&self, _: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        async {
            let content_length = body_to_bytes(response.into_body()).await.len();
            Response::builder()
                .status(hyper::StatusCode::OK)
                .header(CONTENT_LENGTH, content_length)
                .body(Body::wrap_stream(futures::stream::pending::<Result<Vec<u8>, String>>()))
                .expect("valid response")
        }
        .boxed()
    }
}

/// Handler that forwards to its wrapped handler once.
pub struct Once<H: UriPathHandler> {
    already_forwarded: AtomicBool,
    handler: H,
}

impl<H: UriPathHandler> UriPathHandler for Once<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        if self.already_forwarded.fetch_or(true, Ordering::SeqCst) {
            ready(response).boxed()
        } else {
            self.handler.handle(uri_path, response)
        }
    }
}

impl<H: UriPathHandler> Once<H> {
    /// Creates a handler that forwards to `handler` once.
    pub fn new(handler: H) -> Self {
        Self { already_forwarded: AtomicBool::new(false), handler }
    }
}

/// Range Handler that forwards to its wrapped handler once.
pub struct RangeOnce<H: RangeUriPathHandler> {
    already_forwarded: AtomicBool,
    handler: H,
}

impl<H: RangeUriPathHandler> RangeUriPathHandler for RangeOnce<H> {
    fn handle(
        &self,
        uri_path: &Path,
        range: &http::HeaderValue,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if self.already_forwarded.fetch_or(true, Ordering::SeqCst) {
            ready(response).boxed()
        } else {
            self.handler.handle(uri_path, range, response)
        }
    }
}

impl<H: RangeUriPathHandler> RangeOnce<H> {
    /// Creates a Range handler that forwards to `handler` once.
    pub fn new(handler: H) -> Self {
        Self { already_forwarded: AtomicBool::new(false), handler }
    }
}

/// Handler that forwards to its wrapped handler the nth time it is called.
pub struct OverrideNth<H: UriPathHandler> {
    n: u32,
    call_count: AtomicU32,
    handler: H,
}

impl<H: UriPathHandler> UriPathHandler for OverrideNth<H> {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        if self.call_count.fetch_add(1, Ordering::SeqCst) + 1 == self.n {
            self.handler.handle(uri_path, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: UriPathHandler> OverrideNth<H> {
    /// Creates a handler that forwards to `handler` on the nth call.
    pub fn new(n: u32, handler: H) -> Self {
        Self { n, call_count: AtomicU32::new(0), handler }
    }
}

/// Information record by RecordingRange for each request it handles.
pub struct RangeHistoryEntry {
    uri_path: PathBuf,
    range: http::HeaderValue,
}

impl RangeHistoryEntry {
    /// The uri_path of the request.
    pub fn uri_path(&self) -> &Path {
        &self.uri_path
    }

    /// The "Range" header of the request.
    pub fn range(&self) -> &http::HeaderValue {
        &self.range
    }
}

/// The request history recorded by RecordingRange.
pub struct RangeHistory(Arc<Mutex<Vec<RangeHistoryEntry>>>);

impl RangeHistory {
    /// Take the recorded history, clearing it from the RecordingRange.
    pub fn take(&self) -> Vec<RangeHistoryEntry> {
        std::mem::replace(&mut self.0.lock(), vec![])
    }
}

/// Handler that records the Range headers
pub struct RecordingRange {
    history: RangeHistory,
}

impl RecordingRange {
    /// Creates a handler that records all the requests.
    pub fn new() -> (Self, RangeHistory) {
        let history = Arc::new(Mutex::new(vec![]));
        (Self { history: RangeHistory(Arc::clone(&history)) }, RangeHistory(history))
    }
}

impl RangeUriPathHandler for RecordingRange {
    fn handle(
        &self,
        uri_path: &Path,
        range: &http::HeaderValue,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        self.history
            .0
            .lock()
            .push(RangeHistoryEntry { uri_path: uri_path.to_owned(), range: range.clone() });
        ready(response).boxed()
    }
}

/// Information saved by Record for each request it handles.
#[derive(Debug)]
pub struct HistoryEntry {
    uri_path: PathBuf,
}

impl HistoryEntry {
    /// The uri_path of the request.
    pub fn uri_path(&self) -> &Path {
        &self.uri_path
    }
}

/// The request history recorded by Record.
pub struct History(Arc<Mutex<Vec<HistoryEntry>>>);

impl History {
    /// Take the recorded history, clearing it from the Record.
    pub fn take(&self) -> Vec<HistoryEntry> {
        std::mem::replace(&mut self.0.lock(), vec![])
    }
}

/// Responder that records the requests.
pub struct Record {
    history: History,
}

impl Record {
    /// Creates a responder that records all the requests.
    pub fn new() -> (Self, History) {
        let history = Arc::new(Mutex::new(vec![]));
        (Self { history: History(Arc::clone(&history)) }, History(history))
    }
}

impl UriPathHandler for Record {
    fn handle<'a>(
        &'a self,
        uri_path: &Path,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        self.history.0.lock().push(HistoryEntry {
            uri_path: uri_path.to_owned(),
        });
        ready(response).boxed()
    }
}
