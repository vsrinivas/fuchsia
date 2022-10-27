// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! HttpResponder implementations

use {
    crate::serve::HttpResponder,
    futures::{
        channel::{mpsc, oneshot},
        future::{pending, ready, BoxFuture, Shared},
        prelude::*,
    },
    hyper::{Body, Request, Response, StatusCode},
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

/// hyper::Request extension trait that makes writing `HttpResponder`s more convienent.
pub trait RequestExt {
    /// The URI path of the Request.
    fn path(&self) -> &Path;
}

impl RequestExt for Request<Body> {
    /// The URI path of the Request.
    fn path(&self) -> &Path {
        Path::new(self.uri().path())
    }
}

/// Responder that always responds with the given status code
pub struct StaticResponseCode(StatusCode);

impl HttpResponder for StaticResponseCode {
    fn respond(&self, _: &Request<Body>, _: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        ready(Response::builder().status(self.0).body(Body::empty()).unwrap()).boxed()
    }
}

impl StaticResponseCode {
    /// Creates responder that always responds with the given status code
    pub fn new(status: StatusCode) -> Self {
        Self(status)
    }

    /// Creates responder that always responds with 200 OK
    pub fn ok() -> Self {
        Self(StatusCode::OK)
    }

    /// Creates responder that always responds with 404 Not Found
    pub fn not_found() -> Self {
        Self(StatusCode::NOT_FOUND)
    }

    /// Creates responder that always responds with 500 Internal Server Error
    pub fn server_error() -> Self {
        Self(StatusCode::INTERNAL_SERVER_ERROR)
    }

    /// Creates responder that always responds with 429 Too Many Requests
    pub fn too_many_requests() -> Self {
        Self(StatusCode::TOO_MANY_REQUESTS)
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

/// Responder that replies with an externally-settable HTTP status.
pub struct DynamicResponseCode {
    code: Arc<AtomicU16>,
}

impl HttpResponder for DynamicResponseCode {
    fn respond<'a>(
        &'a self,
        _: &'a Request<Body>,
        _: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
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
    /// Creates a new responder with a (re)settable status code.
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

/// Responder that overrides requests with the given responder only when enabled
pub struct Toggleable<H: HttpResponder> {
    enabled: Arc<AtomicBool>,
    responder: H,
}

impl<H: HttpResponder> HttpResponder for Toggleable<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if self.enabled.load(Ordering::SeqCst) {
            self.responder.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: HttpResponder> Toggleable<H> {
    /// Creates responder that overrides requests when should_override is set.
    pub fn new(should_override: &AtomicToggle, responder: H) -> Self {
        Self { enabled: Arc::clone(&should_override.0), responder }
    }
}

/// Responder that overrides the given request path for the given number of requests.
pub struct ForRequestCount<H: HttpResponder> {
    remaining: Mutex<u32>,
    responder: H,
}

impl<H: HttpResponder> HttpResponder for ForRequestCount<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        let mut remaining = self.remaining.lock();
        if *remaining > 0 {
            *remaining -= 1;
            drop(remaining);
            self.responder.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: HttpResponder> ForRequestCount<H> {
    /// Creates responder that overrides the given request path for the given number of requests.
    pub fn new(count: u32, responder: H) -> Self {
        Self { remaining: Mutex::new(count), responder }
    }
}

/// Responder that overrides the given request path using the given responder.
pub struct ForPath<H: HttpResponder> {
    path: PathBuf,
    responder: H,
}

impl<H: HttpResponder> HttpResponder for ForPath<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if self.path == request.path() {
            self.responder.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: HttpResponder> ForPath<H> {
    /// Creates responder that overrides the given request path using the given responder.
    pub fn new(path: impl Into<PathBuf>, responder: H) -> Self {
        Self { path: path.into(), responder }
    }
}

/// Responder that overrides the given request paths using the given responder.
pub struct ForPaths<H: HttpResponder> {
    paths: HashSet<PathBuf>,
    responder: H,
}

impl<H: HttpResponder> HttpResponder for ForPaths<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if self.paths.contains(request.path()) {
            self.responder.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: HttpResponder> ForPaths<H> {
    /// Creates responder that overrides the given request paths using the given responder.
    pub fn new(paths: HashSet<PathBuf>, responder: H) -> Self {
        Self { paths, responder }
    }
}

/// Responder that overrides all the requests that start with the given request path using the
/// given responder.
pub struct ForPathPrefix<H: HttpResponder> {
    prefix: PathBuf,
    responder: H,
}

impl<H: HttpResponder> HttpResponder for ForPathPrefix<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if request.path().starts_with(&self.prefix) {
            self.responder.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: HttpResponder> ForPathPrefix<H> {
    /// Creates responder that overrides all the requests that start with the given request path
    /// using the given responder.
    pub fn new(prefix: impl Into<PathBuf>, responder: H) -> Self {
        Self { prefix: prefix.into(), responder }
    }
}

/// Responder that overrides all the requests that end with the given request path using the
/// given responder. Useful for hitting all versions of versioned TUF metadata (e.g.
/// X.targets.json).
/// TODO(ampearce): change ForPathSuffix and ForPathPrefix to use string matches rather than path.
pub struct ForPathSuffix<H: HttpResponder> {
    suffix: PathBuf,
    responder: H,
}

impl<H: HttpResponder> HttpResponder for ForPathSuffix<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if request.path().ends_with(&self.suffix) {
            self.responder.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: HttpResponder> ForPathSuffix<H> {
    /// Creates responder that overrides all the requests that start with the given request path
    /// using the given responder.
    pub fn new(suffix: impl Into<PathBuf>, responder: H) -> Self {
        Self { suffix: suffix.into(), responder }
    }
}
/// Responder that passes responses through the given responder once per unique path.
pub struct OncePerPath<H: HttpResponder> {
    responder: H,
    failed_paths: Mutex<HashSet<PathBuf>>,
}

impl<H: HttpResponder> HttpResponder for OncePerPath<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if self.failed_paths.lock().insert(request.path().to_owned()) {
            self.responder.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: HttpResponder> OncePerPath<H> {
    /// Creates responder that passes responses through the given responder once per unique path.
    pub fn new(responder: H) -> Self {
        Self { responder, failed_paths: Mutex::new(HashSet::new()) }
    }
}

/// Transform a `serde_json::Value`. Implements `HttpResponder` by assuming the `Response<Body>` is
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

/// Responder that manipulates requests with json-formatted bodies.
impl<T: JsonTransformer> HttpResponder for T {
    fn respond(
        &self,
        _: &Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        async move {
            let (mut parts, body) = response.into_parts();
            parts.headers.remove(http::header::CONTENT_LENGTH);
            let bytes = body_to_bytes(body).await;
            let value = self.transform(serde_json::from_reader(bytes.as_slice()).unwrap());
            let bytes = serde_json::to_vec(&value).unwrap();
            Response::from_parts(parts, Body::from(bytes))
        }
        .boxed()
    }
}

/// Responder that notifies a channel when it receives a request.
pub struct NotifyWhenRequested {
    notify: mpsc::UnboundedSender<()>,
}

impl NotifyWhenRequested {
    /// Creates a new responder and the receiver it notifies on request receipt.
    pub fn new() -> (Self, mpsc::UnboundedReceiver<()>) {
        let (tx, rx) = mpsc::unbounded();
        (Self { notify: tx }, rx)
    }
}

impl HttpResponder for NotifyWhenRequested {
    fn respond(
        &self,
        _: &Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
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

/// Responder that blocks sending response headers and bodies until unblocked by a test.
pub struct BlockResponseHeaders {
    blocked_responses: mpsc::UnboundedSender<BlockedResponse>,
}

impl BlockResponseHeaders {
    /// Creates a new responder and the receiver it notifies on request receipt.
    pub fn new() -> (Self, mpsc::UnboundedReceiver<BlockedResponse>) {
        let (sender, receiver) = mpsc::unbounded();

        (Self { blocked_responses: sender }, receiver)
    }
}

impl HttpResponder for BlockResponseHeaders {
    fn respond(
        &self,
        request: &Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        // Return a future that notifies the test that the request was blocked and wait for it to
        // unblock the response.
        let path = request.path().to_owned();
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

/// Responder that blocks sending response bodies until unblocked by a test.
pub struct BlockResponseBodyOnce {
    #[allow(clippy::type_complexity)]
    notify: Mutex<Option<oneshot::Sender<Box<dyn FnOnce() + Send>>>>,
}

impl BlockResponseBodyOnce {
    /// Creates a new responder and the receiver it notifies after sending the response headers.
    pub fn new() -> (Self, oneshot::Receiver<Box<dyn FnOnce() + Send>>) {
        let (sender, receiver) = oneshot::channel();

        (Self { notify: Mutex::new(Some(sender)) }, receiver)
    }
}

impl HttpResponder for BlockResponseBodyOnce {
    fn respond(
        &self,
        _: &Request<Body>,
        mut response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
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

            // Yield the modified response so hyper will send the headers and wait for the body to
            // be unblocked.
            response
        }
        .boxed()
    }
}

async fn body_to_bytes(body: Body) -> Vec<u8> {
    hyper::body::to_bytes(body).await.expect("body to bytes").to_vec()
}

/// Responder that yields the response up to the final byte, then produces an error.  Panics if the
/// response contains an empty body.
pub struct OneByteShortThenError;

impl HttpResponder for OneByteShortThenError {
    fn respond(
        &self,
        _: &Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        async {
            let (parts, body) = response.into_parts();
            let mut bytes = body_to_bytes(body).await;
            if bytes.pop().is_none() {
                panic!("can't short 0 bytes");
            }
            Response::from_parts(
                parts,
                Body::wrap_stream(futures::stream::iter(vec![
                    Ok(bytes),
                    Err("all_but_one_byte_then_eror has sent all but one bytes".to_string()),
                ])),
            )
        }
        .boxed()
    }
}

/// Responder that yields the response up to the Nth byte, then produces an error.  Panics if the
/// response does not contain more than N bytes.
pub struct NBytesThenError {
    n: usize,
}

impl NBytesThenError {
    /// Make a responder that returns N bytes then errors.
    pub fn new(n: usize) -> Self {
        Self { n }
    }
}
impl HttpResponder for NBytesThenError {
    fn respond(
        &self,
        _: &Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        let n = self.n;
        async move {
            let (parts, body) = response.into_parts();
            let mut bytes = body_to_bytes(body).await;
            let initial_len = bytes.len();
            if initial_len <= n {
                panic!("not enough bytes to shorten, {} {}", initial_len, n);
            }
            bytes.truncate(n);
            Response::from_parts(
                parts,
                Body::wrap_stream(futures::stream::iter(vec![
                    Ok(bytes),
                    Err("all_but_one_byte_then_eror has sent all but one bytes".to_string()),
                ])),
            )
        }
        .boxed()
    }
}

/// Responder that yields the response up to the final byte, then disconnects.  Panics if the
/// response contains an empty body.
pub struct OneByteShortThenDisconnect;

impl HttpResponder for OneByteShortThenDisconnect {
    fn respond(
        &self,
        _: &Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        async {
            let (parts, body) = response.into_parts();
            let mut bytes = body_to_bytes(body).await;
            if bytes.pop().is_none() {
                panic!("can't short 0 bytes");
            }
            Response::from_parts(
                parts,
                Body::wrap_stream(futures::stream::iter(vec![Result::<Vec<u8>, String>::Ok(
                    bytes,
                )])),
            )
        }
        .boxed()
    }
}

/// Responder that flips the first byte of the response.  Panics if the response contains an empty
/// body.
pub struct OneByteFlipped;

impl HttpResponder for OneByteFlipped {
    fn respond(
        &self,
        _: &Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        async {
            let (parts, body) = response.into_parts();
            let mut bytes = body_to_bytes(body).await;
            if bytes.is_empty() {
                panic!("can't flip 0 bytes");
            }
            bytes[0] = !bytes[0];
            Response::from_parts(parts, Body::from(bytes))
        }
        .boxed()
    }
}

/// Responder that never sends bytes.
pub struct Hang;

impl HttpResponder for Hang {
    fn respond(&self, _: &Request<Body>, _: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        pending().boxed()
    }
}

/// Responder that sends the header but then never sends body bytes.
pub struct HangBody;

impl HttpResponder for HangBody {
    fn respond(
        &self,
        _: &Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        async {
            let (parts, _) = response.into_parts();
            Response::from_parts(
                parts,
                Body::wrap_stream(futures::stream::pending::<Result<Vec<u8>, String>>()),
            )
        }
        .boxed()
    }
}

/// Responder that forwards to its wrapped responder once.
pub struct Once<H: HttpResponder> {
    already_forwarded: AtomicBool,
    responder: H,
}

impl<H: HttpResponder> HttpResponder for Once<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if self.already_forwarded.fetch_or(true, Ordering::SeqCst) {
            ready(response).boxed()
        } else {
            self.responder.respond(request, response)
        }
    }
}

impl<H: HttpResponder> Once<H> {
    /// Creates a responder that forwards to `responder` once.
    pub fn new(responder: H) -> Self {
        Self { already_forwarded: AtomicBool::new(false), responder }
    }
}

/// Responder that forwards to its wrapped responder the nth time it is called.
pub struct OverrideNth<H: HttpResponder> {
    n: u32,
    call_count: AtomicU32,
    responder: H,
}

impl<H: HttpResponder> HttpResponder for OverrideNth<H> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if self.call_count.fetch_add(1, Ordering::SeqCst) + 1 == self.n {
            self.responder.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

impl<H: HttpResponder> OverrideNth<H> {
    /// Creates a responder that forwards to `responder` on the nth call.
    pub fn new(n: u32, responder: H) -> Self {
        Self { n, call_count: AtomicU32::new(0), responder }
    }
}

/// Information saved by Record for each request it handles.
#[derive(Debug)]
pub struct HistoryEntry {
    uri_path: PathBuf,
    headers: hyper::HeaderMap<hyper::header::HeaderValue>,
}

impl HistoryEntry {
    /// The uri_path of the request.
    pub fn uri_path(&self) -> &Path {
        &self.uri_path
    }

    /// The request headers.
    pub fn headers(&self) -> &http::HeaderMap<hyper::header::HeaderValue> {
        &self.headers
    }
}

/// The request history recorded by Record.
pub struct History(Arc<Mutex<Vec<HistoryEntry>>>);

impl History {
    /// Take the recorded history, clearing it from the Record.
    pub fn take(&self) -> Vec<HistoryEntry> {
        std::mem::take(&mut self.0.lock())
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

impl HttpResponder for Record {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        self.history.0.lock().push(HistoryEntry {
            uri_path: request.path().to_owned(),
            headers: request.headers().clone(),
        });
        ready(response).boxed()
    }
}

/// Responder that forwards requests to its wrapped Responder if filter returns true.
pub struct Filter<F: FilterFn, T: HttpResponder> {
    filter: F,
    handler: T,
}

/// Used by the Filter HttpResponder to decide which requests to forward and which to ignore.
pub trait FilterFn: Send + Sync + 'static {
    /// Return true iff Filter should forward the request to its wrapped Responder.
    fn filter(&self, request: &Request<Body>) -> bool;
}

impl<F> FilterFn for F
where
    F: Fn(&Request<Body>) -> bool + Send + Sync + 'static,
{
    fn filter(&self, request: &Request<Body>) -> bool {
        (self)(request)
    }
}

/// Returns true iff the request has a Content-Range header.
pub fn is_range_request(request: &Request<Body>) -> bool {
    request.headers().get(http::header::RANGE).is_some()
}

impl<F: FilterFn, T: HttpResponder> Filter<F, T> {
    /// Creates a responder that forwards requests that satisfy a filter.
    pub fn new(filter: F, handler: T) -> Self {
        Self { filter, handler }
    }
}

impl<F: FilterFn, T: HttpResponder> HttpResponder for Filter<F, T> {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        if self.filter.filter(request) {
            self.handler.respond(request, response)
        } else {
            ready(response).boxed()
        }
    }
}

/// Responder that changes the status code to a given value.
pub struct OverwriteStatusCode {
    code: http::StatusCode,
}

impl OverwriteStatusCode {
    /// Creates a responder that changes the status code to a given value.
    pub fn new(code: http::StatusCode) -> Self {
        Self { code }
    }
}

impl HttpResponder for OverwriteStatusCode {
    fn respond(
        &self,
        _: &Request<Body>,
        mut response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        *response.status_mut() = self.code;
        futures::future::ready(response).boxed()
    }
}

/// Responder that calls each wrapped responder in order.
pub struct Chain {
    responders: Vec<Box<dyn HttpResponder>>,
}

impl Chain {
    /// Creates a responder that calls each wrapped responder in order.
    pub fn new(responders: Vec<Box<dyn HttpResponder>>) -> Self {
        Self { responders }
    }
}

impl HttpResponder for Chain {
    fn respond<'a>(
        &'a self,
        request: &'a Request<Body>,
        mut response: Response<Body>,
    ) -> BoxFuture<'a, Response<Body>> {
        async move {
            for responder in self.responders.iter() {
                response = responder.respond(request, response).await;
            }
            response
        }
        .boxed()
    }
}

/// Fails all requests with NOT_FOUND.
/// All requests made to the first requested path are failed immmediately.
/// All requests to subsequent paths are blocked until the `unblocker` returned by
///   new() is used, at which point all requests (pending and future) fail immediately.
pub struct FailOneThenTemporarilyBlock {
    path_to_fail: Arc<Mutex<Option<PathBuf>>>,
    block_until: Shared<oneshot::Receiver<()>>,
}

impl FailOneThenTemporarilyBlock {
    /// Create a FailOneThenTemporarilyBlock and its paired unblocker.
    pub fn new() -> (Self, oneshot::Sender<()>) {
        let (send, recv) = oneshot::channel();
        (Self { path_to_fail: Arc::new(Mutex::new(None)), block_until: recv.shared() }, send)
    }
}

impl HttpResponder for FailOneThenTemporarilyBlock {
    fn respond(&self, request: &Request<Body>, _: Response<Body>) -> BoxFuture<'_, Response<Body>> {
        let response =
            Response::builder().status(StatusCode::NOT_FOUND).body(Body::empty()).unwrap();
        match &mut *self.path_to_fail.lock() {
            o @ None => {
                *o = Some(request.path().to_owned());
                ready(response).boxed()
            }
            Some(path_to_fail) if path_to_fail == request.path() => ready(response).boxed(),
            _ => {
                let block_until = self.block_until.clone();
                async move {
                    block_until.await.unwrap();
                    response
                }
            }
            .boxed(),
        }
    }
}
