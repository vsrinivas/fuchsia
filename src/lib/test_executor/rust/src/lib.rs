// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate runs and collects results from a test which implements fuchsia.test.Suite protocol.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::handle::AsHandleRef,
    fidl_fuchsia_test::{
        CaseListenerRequest::Finished,
        CaseListenerRequestStream, Invocation,
        RunListenerRequest::{OnFinished, OnTestCaseStarted},
        SuiteProxy,
    },
    fidl_fuchsia_test_manager::{
        self as ftest_manager, HarnessProxy, LaunchOptions, LegacySuiteControllerProxy,
        SuiteControllerProxy, SuiteEvent as FidlSuiteEvent,
        SuiteEventPayload as FidlSuiteEventPayload,
    },
    fuchsia_async as fasync,
    futures::{
        channel::mpsc,
        future::{join_all, try_join3},
        io::{self, AsyncRead},
        lock::Mutex,
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    glob,
    linked_hash_map::LinkedHashMap,
    log::*,
    std::{
        cell::RefCell, collections::HashMap, marker::Unpin, pin::Pin, sync::Arc, time::Duration,
    },
};

pub use crate::diagnostics::{LogStream, LogStreamProtocol};

mod diagnostics;

/// Buffer logs for this duration before flushing them.
const LOG_BUFFERING_DURATION: std::time::Duration = std::time::Duration::from_secs(5);

/// Maximum log buffer size.
const LOG_BUFFER_SIZE: usize = 4096;

/// Duration after which to emit an excessive duration event.
const EXCESSIVE_DURATION: Duration = Duration::from_secs(60);

/// Options that apply when executing a test suite.
///
/// For the FIDL equivalent, see [`fidl_fuchsia_test::RunOptions`].
#[derive(Debug, Clone, Default, Eq, PartialEq)]
pub struct TestRunOptions {
    /// How to handle tests that were marked disabled/ignored by the developer.
    pub disabled_tests: DisabledTestHandling,

    /// Number of test cases to run in parallel.
    pub parallel: Option<u16>,

    /// Arguments passed to tests.
    pub arguments: Vec<String>,
}

/// Event to send to caller of `run_test_component`
#[derive(PartialEq, Debug, Eq, Hash, Ord, PartialOrd, Clone)]
pub enum TestEvent {
    /// A new test case is started.
    TestCaseStarted { test_case_name: String },

    /// Test case finished.
    TestCaseFinished { test_case_name: String, result: TestResult },

    /// Test case is still running after a long duration.
    ExcessiveDuration { test_case_name: String, duration: Duration },

    /// Test case produced a stdout message.
    StdoutMessage { test_case_name: String, msg: String },

    /// Test finishes successfully.
    Finish,
}

impl TestEvent {
    pub fn test_case_started(s: &str) -> TestEvent {
        TestEvent::TestCaseStarted { test_case_name: s.to_string() }
    }

    pub fn stdout_message(name: &str, message: &str) -> TestEvent {
        TestEvent::StdoutMessage { test_case_name: name.to_string(), msg: message.to_string() }
    }

    pub fn test_case_finished(name: &str, result: TestResult) -> TestEvent {
        TestEvent::TestCaseFinished { test_case_name: name.to_string(), result: result }
    }

    pub fn excessive_duration(name: &str, duration: Duration) -> TestEvent {
        TestEvent::ExcessiveDuration { test_case_name: name.to_string(), duration }
    }

    pub fn test_finished() -> TestEvent {
        TestEvent::Finish
    }

    /// Returns the name of the test case to which the event belongs, if applicable.
    pub fn test_case_name(&self) -> Option<&String> {
        match self {
            TestEvent::TestCaseStarted { test_case_name }
            | TestEvent::TestCaseFinished { test_case_name, .. }
            | TestEvent::StdoutMessage { test_case_name, .. }
            | TestEvent::ExcessiveDuration { test_case_name, .. } => Some(test_case_name),
            TestEvent::Finish => None,
            // NOTE: If new global event types (not tied to a specific test case) are added,
            // `GroupByTestCase` must also be updated so as to preserve correct event ordering.
            // Otherwise, all such events will end up with a key of `None` in the map, and might
            // therefore be spuriously bunched together.
        }
    }

    /// Same as `test_case_name`, but returns an owned `Option<String>`.
    pub fn owned_test_case_name(&self) -> Option<String> {
        self.test_case_name().map(String::from)
    }
}

/// Trait allowing iterators over `TestEvent` to be partitioned by test case name.
///
/// Note that the current implementation assumes that the only `TestEvent` type that occurs
/// _outside of test cases_ is `TestEvent::Finish`. If new global `TestEvent` types are added,
/// the implementation will have to be changed.
pub trait GroupByTestCase: Iterator<Item = TestEvent> + Sized {
    /// Groups the `TestEvent`s by test case name into a map that preserves insertion order.
    /// The overall order of test cases (by first event) and the orders of events within each test
    /// case are preserved, but events from different test cases are effectively de-interleaved.
    ///
    /// Example:
    /// ```rust
    /// use test_executor::{TestEvent, GroupByTestCase as _};
    /// use linked_hash_map::LinkedHashMap;
    ///
    /// let events: Vec<TestEvent> = get_events();
    /// let grouped: LinkedHashMap<Option<String>, TestEvent> =
    ///     events.into_iter().group_by_test_case();
    /// ```
    fn group_by_test_case_ordered(self) -> LinkedHashMap<Option<String>, Vec<TestEvent>> {
        let mut map = LinkedHashMap::new();
        for test_event in self {
            map.entry(test_event.owned_test_case_name()).or_insert(Vec::new()).push(test_event);
        }
        map
    }

    /// De-interleaves the `TestEvents` by test case. The overall order of test cases (by first
    /// event) and the orders of events within each test case are preserved.
    fn deinterleave(self) -> Box<dyn Iterator<Item = TestEvent>> {
        Box::new(
            self.group_by_test_case_ordered()
                .into_iter()
                .flat_map(|(_, events)| events.into_iter()),
        )
    }

    /// Groups the `TestEvent`s by test case name into an unordered map. The orders of events within
    /// each test case are preserved, but the test cases themselves are not in a defined order.
    fn group_by_test_case_unordered(self) -> HashMap<Option<String>, Vec<TestEvent>> {
        let mut map = HashMap::new();
        for test_event in self {
            map.entry(test_event.owned_test_case_name()).or_insert(Vec::new()).push(test_event);
        }
        map
    }
}

impl<T> GroupByTestCase for T where T: Iterator<Item = TestEvent> + Sized {}

/// How to handle tests that were marked disabled/ignored by the developer.
#[derive(Debug, Clone, Eq, PartialEq)]
pub enum DisabledTestHandling {
    /// Skip tests that were marked disabled/ignored by the developer.
    Exclude,
    /// Explicitly include tests that were marked disabled/ignored by the developer.
    Include,
}

impl Default for DisabledTestHandling {
    fn default() -> Self {
        DisabledTestHandling::Exclude
    }
}

impl From<TestRunOptions> for fidl_fuchsia_test::RunOptions {
    fn from(test_run_options: TestRunOptions) -> Self {
        // Note: This will *not* break if new members are added to the FIDL table.
        fidl_fuchsia_test::RunOptions {
            parallel: test_run_options.parallel,
            arguments: Some(test_run_options.arguments),
            include_disabled_tests: Some(matches!(
                test_run_options.disabled_tests,
                DisabledTestHandling::Include
            )),
            ..fidl_fuchsia_test::RunOptions::EMPTY
        }
    }
}

/// Defines the result of a test case run.
#[derive(PartialEq, Debug, Eq, Hash, Ord, PartialOrd, Copy, Clone)]
pub enum TestResult {
    /// Test case passed.
    Passed,
    /// Test case failed.
    Failed,
    /// Test case skipped.
    Skipped,
    /// Test case did not communicate the result.
    Error,
}

#[must_use = "futures/streams"]
pub struct StdoutStream {
    socket: fidl::AsyncSocket,
}
impl Unpin for StdoutStream {}

thread_local! {
    pub static BUFFER:
        RefCell<[u8; 2048]> = RefCell::new([0; 2048]);
}

impl StdoutStream {
    /// Creates a new `StdoutStream` for given `socket`.
    pub fn new(socket: fidl::Socket) -> Result<StdoutStream, anyhow::Error> {
        let stream = StdoutStream {
            socket: fidl::AsyncSocket::from_socket(socket).context("Invalid zircon socket")?,
        };
        Ok(stream)
    }
}

fn process_stdout_bytes(bytes: &[u8]) -> String {
    // TODO(anmittal): Change this to consider break in logs and handle it.
    let log = String::from_utf8_lossy(bytes);
    log.to_string()
}

impl Stream for StdoutStream {
    type Item = io::Result<String>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        BUFFER.with(|b| {
            let mut b = b.borrow_mut();
            let len = ready!(Pin::new(&mut self.socket).poll_read(cx, &mut *b)?);
            if len == 0 {
                return Poll::Ready(None);
            }
            Poll::Ready(Some(process_stdout_bytes(&b[0..len])).map(Ok))
        })
    }
}

#[derive(Clone)]
enum LogEventSender {
    TestEvent(mpsc::Sender<TestEvent>),
    SuiteEvent(mpsc::Sender<SuiteEvent>),
}

impl From<mpsc::Sender<TestEvent>> for LogEventSender {
    fn from(sender: mpsc::Sender<TestEvent>) -> Self {
        LogEventSender::TestEvent(sender)
    }
}

impl From<mpsc::Sender<SuiteEvent>> for LogEventSender {
    fn from(sender: mpsc::Sender<SuiteEvent>) -> Self {
        LogEventSender::SuiteEvent(sender)
    }
}

struct LogOpt {
    /// Socket of which test serves stdout.
    stdout_socket: fidl::Socket,
    /// Send Log Event over this channel.
    sender: mpsc::Sender<String>,
    /// Duration to buffer the logs before flushing them.
    buffering_duration: std::time::Duration,
    /// MAx buffer size before logs are flushed.
    buffer_size: usize,
}

struct TestCaseProcessor {
    f: fuchsia_async::Task<Result<(), anyhow::Error>>,
}

impl TestCaseProcessor {
    /// This will start processing of stdout logs and events in the background. The owner of this
    /// object should call `wait_for_finish` method  to make sure all the background task completed.
    pub fn new(
        test_case_name: String,
        listener: CaseListenerRequestStream,
        stdout_socket: fidl::Socket,
        mut sender: mpsc::Sender<TestEvent>,
    ) -> Self {
        let (s, mut stdout_recv) = mpsc::channel(1024);
        let stdout_fut = collect_and_send_stdout(stdout_socket, s);
        let mut s_clone = sender.clone();
        let test_case_name_clone = test_case_name.clone();
        let stdout_collector = async move {
            while let Some(msg) = stdout_recv.next().await {
                s_clone
                    .send(TestEvent::stdout_message(&test_case_name_clone, &msg))
                    .await
                    .context("Error sending stdout msg")?;
            }
            Ok(())
        };
        let test_complete_fut = Self::listen_for_completion(listener);

        let fut = async move {
            let mut sender_clone = sender.clone();
            let test_case_name_clone = test_case_name.clone();
            let excessive_time_task = fasync::Task::spawn(async move {
                fasync::Timer::new(EXCESSIVE_DURATION).await;
                sender_clone
                    .send(TestEvent::excessive_duration(&test_case_name_clone, EXCESSIVE_DURATION))
                    .await
                    .context("Failed to send excessive duration event")
            });
            let ((), (), result) =
                try_join3(stdout_fut, stdout_collector, test_complete_fut).await?;
            excessive_time_task.cancel().await.unwrap_or(Ok(()))?;
            if let Some(result) = result {
                sender
                    .send(TestEvent::test_case_finished(&test_case_name, result))
                    .await
                    .context("Failed to send TestCaseFinished Event")
            } else {
                Ok(())
            }
        };

        TestCaseProcessor { f: fuchsia_async::Task::spawn(fut) }
    }

    /// Listen for test completion on the given |listener|. Returns None if the channel is closed
    /// before a test completion event.
    async fn listen_for_completion(
        mut listener: CaseListenerRequestStream,
    ) -> Result<Option<TestResult>, anyhow::Error> {
        if let Some(request) = listener.try_next().await.context("waiting for listener")? {
            let Finished { result, control_handle: _ } = request;
            let result = match result.status {
                Some(status) => match status {
                    fidl_fuchsia_test::Status::Passed => TestResult::Passed,
                    fidl_fuchsia_test::Status::Failed => TestResult::Failed,
                    fidl_fuchsia_test::Status::Skipped => TestResult::Skipped,
                },
                // This will happen when test protocol is not properly implemented
                // by the test and it forgets to set the result.
                None => TestResult::Error,
            };
            Ok(Some(result))
        } else {
            Ok(None)
        }
    }

    /// This will wait for all the stdout logs and events to be collected
    pub async fn wait_for_finish(self) -> Result<(), anyhow::Error> {
        self.f.await.context("Failure waiting for stdout and events")
    }
}

pub async fn collect_and_send_stdout(
    socket: fidl::Socket,
    sender: mpsc::Sender<String>,
) -> Result<(), anyhow::Error> {
    collect_and_send_stdout_internal(LogOpt {
        stdout_socket: socket,
        sender,
        buffering_duration: LOG_BUFFERING_DURATION,
        buffer_size: LOG_BUFFER_SIZE,
    })
    .await
}
/// Internal method that put a listener on `stdout_socket`, process and send test stdout logs
/// asynchronously in the background. Returns immediately if the provided socket is an invalid
/// handle.
async fn collect_and_send_stdout_internal(log_opt: LogOpt) -> Result<(), anyhow::Error> {
    if log_opt.stdout_socket.as_handle_ref().is_invalid() {
        return Ok(());
    }

    let mut stream = match StdoutStream::new(log_opt.stdout_socket) {
        Err(e) => {
            error!("Stdout Logger: Failed to create fuchsia async socket: {:?}", e);
            return Ok(());
        }
        Ok(stream) => stream,
    };
    let mut log_buffer =
        StdoutBuffer::new(log_opt.buffering_duration, log_opt.sender, log_opt.buffer_size);

    while let Some(log) = stream.try_next().await.context("Error reading stdout log msg")? {
        log_buffer.send_log(&log).await?;
    }
    log_buffer.done().await
}

/// Buffers logs in memory for `duration` before sending it out.
/// This will not buffer any more logs after timer expires and will flush all
/// subsequent logs instantly.
/// Clients may call done() to obtain any errors. If not called, done() will be called when the
/// buffer is dropped and any errors will be suppressed.
struct StdoutBuffer {
    inner: Arc<Mutex<StdoutBufferInner>>,
    timer: fuchsia_async::Task<()>,
}

impl StdoutBuffer {
    /// Crates new StdoutBuffer and starts the timer on log buffering.
    /// `duration`: Buffers log for this duration or till done() is called.
    /// `sender`: Channel to send logs on.
    /// `max_capacity`: Flush log if buffer size exceeds this value. This will not cancel the timer
    /// and all the logs would be flushed once timer expires.
    pub fn new(
        duration: std::time::Duration,
        sender: mpsc::Sender<String>,
        max_capacity: usize,
    ) -> Self {
        let inner = StdoutBufferInner::new(sender, max_capacity);
        let timer = fuchsia_async::Timer::new(duration);
        let log_buffer = Arc::downgrade(&inner);
        let f = async move {
            timer.await;
            if let Some(log_buffer) = log_buffer.upgrade() {
                let mut log_buffer = log_buffer.lock().await;
                if let Err(e) = log_buffer.stop_buffering().await {
                    log_buffer.set_error(e);
                }
            }
        };

        let timer = fuchsia_async::Task::spawn(f);

        Self { inner, timer }
    }

    /// This will abort the timer (if not already fired) and then flush all
    /// the logs in buffer.
    /// This function should be called before the object is dropped.
    /// Returns error due flushing the logs.
    pub async fn done(self) -> Result<(), anyhow::Error> {
        // abort timer so that it is not fired in the future.
        self.timer.cancel().await;

        let mut inner = self.inner.lock().await;
        inner.flush().await?;
        inner.error.take().map_or_else(|| Ok(()), Err)
    }

    /// This will instantly send logs over the channel if timer has already
    /// fired, otherwise this will buffer the logs.
    ///
    /// Returns error due flushing the logs.
    pub async fn send_log(&mut self, message: &str) -> Result<(), anyhow::Error> {
        let mut inner = self.inner.lock().await;
        inner.send_log(message).await
    }
}

struct StdoutBufferInner {
    logs: String,
    sender: mpsc::Sender<String>,
    /// Whether to buffer logs or not.
    buffer: bool,
    error: Option<anyhow::Error>,
    max_capacity: usize,
}

impl StdoutBufferInner {
    fn new(sender: mpsc::Sender<String>, max_capacity: usize) -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(StdoutBufferInner {
            logs: String::with_capacity(max_capacity),
            sender: sender,
            buffer: true,
            error: None,
            max_capacity,
        }))
    }

    async fn stop_buffering(&mut self) -> Result<(), anyhow::Error> {
        self.buffer = false;
        self.flush().await
    }

    fn set_error(&mut self, e: anyhow::Error) {
        self.error = Some(e);
    }

    async fn flush(&mut self) -> Result<(), anyhow::Error> {
        if !self.logs.is_empty() {
            let ret = Self::send(&mut self.sender, &self.logs).await;
            self.logs.truncate(0);
            return ret;
        }

        Ok(())
    }

    async fn send(sender: &mut mpsc::Sender<String>, message: &str) -> Result<(), anyhow::Error> {
        sender.send(message.into()).await.context("Error sending stdout msg")
    }

    async fn send_log(&mut self, message: &str) -> Result<(), anyhow::Error> {
        if self.buffer {
            self.logs.push_str(message);
            if self.logs.len() >= self.max_capacity {
                return self.flush().await;
            }
            return Ok(());
        }
        Self::send(&mut self.sender, message).await
    }
}

impl Drop for StdoutBufferInner {
    fn drop(&mut self) {
        if !self.logs.is_empty() {
            let message = self.logs.clone();
            let mut sender = self.sender.clone();
            fuchsia_async::Task::spawn(async move {
                if let Err(e) = StdoutBufferInner::send(&mut sender, &message).await {
                    warn!("Error sending logs for {}", e);
                }
            })
            .detach();
        }
    }
}

/// Encapsulates running suite instance.
pub struct SuiteInstance {
    suite: SuiteProxy,
    // For safekeeping so that running component remains alive.
    controller: LegacySuiteControllerProxy,
    log_stream: Option<LogStream>,
}

/// Options with which a `SuiteInstance` can be created
pub struct SuiteInstanceOpts<'a> {
    /// The test harness connection.
    pub harness: &'a HarnessProxy,

    /// The URL of the test to run.
    pub test_url: &'a str,

    /// Whether or not to force a specific protocol streaming for fetching logs. When unset, it'll
    /// default to `BatchIterator` when running on a Fuchsia system or `ArchiveIterator` when
    /// running on a host system. When running on a host system, only `ArchiveIterator` can be
    /// used, so this option will be ignored.
    pub force_log_protocol: Option<LogStreamProtocol>,
}

impl SuiteInstance {
    /// Launches the test and returns an encapsulated object.
    pub async fn new(opts: SuiteInstanceOpts<'_>) -> Result<Self, anyhow::Error> {
        if !opts.test_url.ends_with(".cm") {
            return Err(anyhow::anyhow!(
                "Tried to run a component as a v2 test that doesn't have a .cm extension"
            ));
        }

        let (suite, controller, log_stream) = Self::launch_test_suite(opts).await?;
        Ok(Self { suite, controller, log_stream: Some(log_stream) })
    }

    /// Kill the suite instance.
    pub fn kill(&self) -> Result<(), fidl::Error> {
        self.controller.kill()
    }

    /// Take the singleton log stream associated with the current instance.
    pub fn take_log_stream(&mut self) -> Option<LogStream> {
        self.log_stream.take()
    }

    async fn launch_test_suite(
        opts: SuiteInstanceOpts<'_>,
    ) -> Result<(SuiteProxy, LegacySuiteControllerProxy, LogStream), anyhow::Error> {
        let (suite_proxy, suite_server_end) = fidl::endpoints::create_proxy().unwrap();
        let (controller_proxy, controller_server_end) = fidl::endpoints::create_proxy().unwrap();

        debug!("Launching test component `{}`", opts.test_url);
        let mut log_stream = diagnostics::LogStream::create(opts.force_log_protocol)?;
        let options = LaunchOptions {
            logs_iterator: log_stream.take_iterator_server_end(),
            ..LaunchOptions::EMPTY
        };
        opts.harness
            .launch_suite(&opts.test_url, options, suite_server_end, controller_server_end)
            .await
            .context("launch_test call failed")?
            .map_err(|e: fidl_fuchsia_test_manager::LaunchError| {
                anyhow::anyhow!("error launching test: {:?}", e)
            })?;

        Ok((suite_proxy, controller_proxy, log_stream))
    }

    /// Enumerates test and return invocations.
    pub async fn enumerate_tests(
        &self,
        test_filter: &Option<&str>,
    ) -> Result<Vec<Invocation>, anyhow::Error> {
        debug!("enumerating tests");
        let (case_iterator, server_end) = fidl::endpoints::create_proxy()?;
        self.suite.get_tests(server_end).map_err(suite_error).context("getting test cases")?;
        let mut invocations = vec![];
        let pattern = test_filter
            .map(|filter| {
                glob::Pattern::new(filter)
                    .map_err(|e| anyhow::anyhow!("Bad test filter pattern: {}", e))
            })
            .transpose()?;
        loop {
            let cases = case_iterator
                .get_next()
                .await
                .map_err(suite_error)
                .context("Error getting test cases")?;
            if cases.is_empty() {
                break;
            }
            for case in cases {
                let case_name =
                    case.name.ok_or(format_err!("invocation should contain a name."))?;
                if pattern.as_ref().map_or(true, |p| p.matches(&case_name)) {
                    invocations.push(Invocation {
                        name: Some(case_name),
                        tag: None,
                        ..Invocation::EMPTY
                    });
                }
            }
        }

        debug!("invocations: {:#?}", invocations);

        Ok(invocations)
    }

    /// Enumerate tests and then run all the test cases in the suite.
    pub async fn run_and_collect_results(
        &self,
        sender: mpsc::Sender<TestEvent>,
        test_filter: Option<&str>,
        run_options: TestRunOptions,
    ) -> Result<(), anyhow::Error> {
        let invocations =
            self.enumerate_tests(&test_filter).await.context("Cannot enumerate tests")?;
        self.run_and_collect_results_for_invocations(sender, invocations, run_options).await
    }

    /// Runs the test component using `suite` and collects test stdout logs and results.
    pub async fn run_and_collect_results_for_invocations(
        &self,
        mut sender: mpsc::Sender<TestEvent>,
        invocations: Vec<Invocation>,
        run_options: TestRunOptions,
    ) -> Result<(), anyhow::Error> {
        debug!("running tests");
        let mut successful_completion = true; // will remain true, if there are no tests to run.
        let mut invocations_iter = invocations.into_iter();
        let run_options: fidl_fuchsia_test::RunOptions = run_options.into();
        loop {
            const INVOCATIONS_CHUNK: usize = 50;
            let chunk = invocations_iter.by_ref().take(INVOCATIONS_CHUNK).collect::<Vec<_>>();
            if chunk.is_empty() {
                break;
            }
            let res = match self
                .run_invocations(chunk, run_options.clone(), &mut sender)
                .await
                .context("Error running test cases")
            {
                Ok(success) => success,
                Err(e) => {
                    return Err(e);
                }
            };
            successful_completion &= res;
        }
        if successful_completion {
            sender.send(TestEvent::test_finished()).await.context("sending TestFinished event")?;
        }
        Ok(())
    }

    /// Runs the test component using `suite` and collects stdout logs and results.
    async fn run_invocations(
        &self,
        invocations: Vec<Invocation>,
        run_options: fidl_fuchsia_test::RunOptions,
        sender: &mut mpsc::Sender<TestEvent>,
    ) -> Result<bool, anyhow::Error> {
        let (run_listener_client, mut run_listener) =
            fidl::endpoints::create_request_stream::<fidl_fuchsia_test::RunListenerMarker>()
                .context("creating request stream")?;
        self.suite.run(
            &mut invocations.into_iter().map(|i| i.into()),
            run_options,
            run_listener_client,
        )?;

        let mut test_case_processors = Vec::new();
        let mut successful_completion = false;

        while let Some(result_event) =
            run_listener.try_next().await.context("waiting for listener")?
        {
            match result_event {
                OnTestCaseStarted { invocation, primary_log, listener, control_handle: _ } => {
                    let name =
                        invocation.name.ok_or(anyhow::anyhow!("cannot find name in invocation"))?;
                    sender
                        .send(TestEvent::test_case_started(&name))
                        .await
                        .context("Failed to send TestCaseStart event")?;
                    let test_case_processor = TestCaseProcessor::new(
                        name,
                        listener.into_stream().context("Cannot convert listener to stream")?,
                        primary_log,
                        sender.clone(),
                    );
                    test_case_processors.push(test_case_processor);
                }
                OnFinished { .. } => {
                    successful_completion = true;
                    break;
                }
            }
        }

        // await for all invocations to complete for which test case never completed.
        join_all(test_case_processors.into_iter().map(|i| i.wait_for_finish()))
            .await
            .into_iter()
            .collect::<Result<Vec<()>, Error>>()
            .context("Error waiting for all invocations to finish")?;
        Ok(successful_completion)
    }

    /// Consume this instance and returns underlying proxies.
    pub fn into_proxies(self) -> (SuiteProxy, LegacySuiteControllerProxy) {
        return (self.suite, self.controller);
    }
}

fn suite_error(err: fidl::Error) -> anyhow::Error {
    match err {
        fidl::Error::ClientChannelClosed { .. } => anyhow::anyhow!(
            "The test protocol was closed. This may mean `fuchsia.test.Suite` was not \
            configured correctly. Refer to: \
            https://fuchsia.dev/fuchsia-src/development/components/v2/troubleshooting#troubleshoot-test"
        ),
        err => err.into(),
    }
}

/// Builds and runs test suite(s).
pub struct TestBuilder {
    proxy: ftest_manager::RunBuilderProxy,
}

impl TestBuilder {
    /// Create new instance
    pub fn new(proxy: ftest_manager::RunBuilderProxy) -> Self {
        Self { proxy }
    }

    pub fn take_proxy(self) -> ftest_manager::RunBuilderProxy {
        self.proxy
    }

    /// Add suite to run.
    pub async fn add_suite(
        &self,
        test_url: &str,
        run_options: ftest_manager::RunOptions,
    ) -> Result<SuiteRunInstance, Error> {
        let (controller_proxy, controller) =
            fidl::endpoints::create_proxy().context("Cannot create proxy")?;
        self.proxy.add_suite(test_url, run_options, controller)?;
        Ok(SuiteRunInstance { controller_proxy: controller_proxy.into() })
    }

    /// Runs all tests to completion.
    pub async fn run(self) -> Result<(), Error> {
        let (controller_proxy, controller) =
            fidl::endpoints::create_proxy().context("Cannot create proxy")?;
        self.proxy.build(controller).context("Error starting tests")?;
        // wait for test to end
        let v = controller_proxy.get_events().await.context("Cannot wait for tests to end")?;
        if v.len() != 0 {
            return Err(anyhow::format_err!("The vector should have been empty, something wrong with test manager. Please file bug."));
        }
        Ok(())
    }
}

/// Events produced by test suite.
pub struct SuiteEvent {
    pub timestamp: Option<i64>,
    pub payload: SuiteEventPayload,
}

impl SuiteEvent {
    pub fn case_found(timestamp: Option<i64>, name: String) -> Self {
        SuiteEvent { timestamp, payload: SuiteEventPayload::RunEvent(RunEvent::case_found(name)) }
    }

    pub fn case_started(timestamp: Option<i64>, name: String) -> Self {
        SuiteEvent { timestamp, payload: SuiteEventPayload::RunEvent(RunEvent::case_started(name)) }
    }

    pub fn case_stdout<N, L>(timestamp: Option<i64>, name: N, stdout_message: L) -> Self
    where
        N: Into<String>,
        L: Into<String>,
    {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::case_stdout(
                name.into(),
                stdout_message.into(),
            )),
        }
    }

    pub fn case_stopped(
        timestamp: Option<i64>,
        name: String,
        status: ftest_manager::CaseStatus,
    ) -> Self {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::case_stopped(name, status)),
        }
    }

    pub fn case_finished(timestamp: Option<i64>, name: String) -> Self {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::case_finished(name)),
        }
    }

    pub fn suite_finished(timestamp: Option<i64>, status: ftest_manager::SuiteStatus) -> Self {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::suite_finished(status)),
        }
    }

    pub fn suite_log(timestamp: Option<i64>, log_stream: LogStream) -> Self {
        SuiteEvent { timestamp, payload: SuiteEventPayload::SuiteLog { log_stream } }
    }

    pub fn test_case_log(timestamp: Option<i64>, name: String, log_stream: LogStream) -> Self {
        SuiteEvent { timestamp, payload: SuiteEventPayload::TestCaseLog { name, log_stream } }
    }
}

pub enum SuiteEventPayload {
    /// Logger for test suite
    SuiteLog { log_stream: LogStream },

    /// Logger for a test case in suite.
    TestCaseLog { name: String, log_stream: LogStream },

    /// Test events.
    RunEvent(RunEvent),
}

#[derive(PartialEq, Debug, Eq, Hash, Ord, PartialOrd, Clone)]
pub enum RunEvent {
    CaseFound { name: String },
    CaseStarted { name: String },
    CaseStdout { name: String, stdout_message: String },
    CaseStopped { name: String, status: ftest_manager::CaseStatus },
    CaseFinished { name: String },
    SuiteFinished { status: ftest_manager::SuiteStatus },
}

impl RunEvent {
    pub fn case_found<S>(name: S) -> Self
    where
        S: Into<String>,
    {
        Self::CaseFound { name: name.into() }
    }

    pub fn case_started<S>(name: S) -> Self
    where
        S: Into<String>,
    {
        Self::CaseStarted { name: name.into() }
    }

    pub fn case_stdout<S, L>(name: S, stdout_message: L) -> Self
    where
        S: Into<String>,
        L: Into<String>,
    {
        Self::CaseStdout { name: name.into(), stdout_message: stdout_message.into() }
    }

    pub fn case_stopped<S>(name: S, status: ftest_manager::CaseStatus) -> Self
    where
        S: Into<String>,
    {
        Self::CaseStopped { name: name.into(), status }
    }

    pub fn case_finished<S>(name: S) -> Self
    where
        S: Into<String>,
    {
        Self::CaseFinished { name: name.into() }
    }

    pub fn suite_finished(status: ftest_manager::SuiteStatus) -> Self {
        Self::SuiteFinished { status }
    }
    /// Returns the name of the test case to which the event belongs, if applicable.
    pub fn test_case_name(&self) -> Option<&String> {
        match self {
            RunEvent::CaseFound { name }
            | RunEvent::CaseStarted { name }
            | RunEvent::CaseStdout { name, .. }
            | RunEvent::CaseStopped { name, .. }
            | RunEvent::CaseFinished { name } => Some(name),
            RunEvent::SuiteFinished { .. } => None,
        }
    }

    /// Same as `test_case_name`, but returns an owned `Option<String>`.
    pub fn owned_test_case_name(&self) -> Option<String> {
        self.test_case_name().map(String::from)
    }
}

/// Trait allowing iterators over `RunEvent` to be partitioned by test case name.
pub trait GroupRunEventByTestCase: Iterator<Item = RunEvent> + Sized {
    /// Groups the `RunEvent`s by test case name into a map that preserves insertion order.
    /// The overall order of test cases (by first event) and the orders of events within each test
    /// case are preserved, but events from different test cases are effectively de-interleaved.
    ///
    /// Example:
    /// ```rust
    /// use test_executor::{RunEvent, GroupRunEventByTestCase as _};
    /// use linked_hash_map::LinkedHashMap;
    ///
    /// let events: Vec<RunEvent> = get_events();
    /// let grouped: LinkedHashMap<Option<String>, RunEvent> =
    ///     events.into_iter().group_by_test_case();
    /// ```
    fn group_by_test_case_ordered(self) -> LinkedHashMap<Option<String>, Vec<RunEvent>> {
        let mut map = LinkedHashMap::new();
        for run_event in self {
            map.entry(run_event.owned_test_case_name()).or_insert(Vec::new()).push(run_event);
        }
        map
    }

    /// De-interleaves the `RunEvents` by test case. The overall order of test cases (by first
    /// event) and the orders of events within each test case are preserved.
    fn deinterleave(self) -> Box<dyn Iterator<Item = RunEvent>> {
        Box::new(
            self.group_by_test_case_ordered()
                .into_iter()
                .flat_map(|(_, events)| events.into_iter()),
        )
    }

    /// Groups the `RunEvent`s by test case name into an unordered map. The orders of events within
    /// each test case are preserved, but the test cases themselves are not in a defined order.
    fn group_by_test_case_unordered(self) -> HashMap<Option<String>, Vec<RunEvent>> {
        let mut map = HashMap::new();
        for run_event in self {
            map.entry(run_event.owned_test_case_name()).or_insert(Vec::new()).push(run_event);
        }
        map
    }
}

impl<T> GroupRunEventByTestCase for T where T: Iterator<Item = RunEvent> + Sized {}

#[derive(Default)]
struct FidlSuiteEventProcessor {
    case_map: HashMap<u32, String>,
    stdout_map: HashMap<u32, Vec<fasync::Task<Result<(), Error>>>>,
}

impl FidlSuiteEventProcessor {
    fn new() -> Self {
        FidlSuiteEventProcessor::default()
    }

    fn get_test_case_name(&self, identifier: u32) -> String {
        self.case_map
            .get(&identifier)
            .expect(&format!("invalid test case identifier: {}", identifier))
            .clone()
    }

    async fn process(
        &mut self,
        event: FidlSuiteEvent,
        mut sender: mpsc::Sender<SuiteEvent>,
    ) -> Result<(), Error> {
        let timestamp = event.timestamp;
        let e = match event.payload.expect("Payload cannot be null, please file bug.") {
            FidlSuiteEventPayload::CaseFound(cf) => {
                self.case_map.insert(cf.identifier, cf.test_case_name.clone());
                SuiteEvent::case_found(timestamp, cf.test_case_name).into()
            }
            FidlSuiteEventPayload::CaseStarted(cs) => {
                let test_case_name = self.get_test_case_name(cs.identifier);
                SuiteEvent::case_started(timestamp, test_case_name).into()
            }
            FidlSuiteEventPayload::CaseStopped(cs) => {
                let test_case_name = self.get_test_case_name(cs.identifier);
                if let Some(stdouts) = self.stdout_map.remove(&cs.identifier) {
                    for s in stdouts {
                        s.await
                            .context(format!("error collecting stdout of {}", test_case_name))?;
                    }
                }
                SuiteEvent::case_stopped(timestamp, test_case_name, cs.status).into()
            }
            FidlSuiteEventPayload::CaseFinished(cf) => {
                let test_case_name = self.get_test_case_name(cf.identifier);
                SuiteEvent::case_finished(timestamp, test_case_name).into()
            }
            FidlSuiteEventPayload::CaseArtifact(ca) => {
                let name = self.get_test_case_name(ca.identifier);
                match ca.artifact {
                    ftest_manager::Artifact::Stdout(stdout) => {
                        let (s, mut r) = mpsc::channel(1024);
                        let stdout_task = fasync::Task::spawn(collect_and_send_stdout(stdout, s));
                        let mut sender_clone = sender.clone();
                        let send_stdout_task = fasync::Task::spawn(async move {
                            while let Some(msg) = r.next().await {
                                sender_clone
                                    .send(SuiteEvent::case_stdout(None, &name, msg))
                                    .await
                                    .context(format!("cannot send logs for {}", name))?;
                            }
                            Ok(())
                        });
                        match self.stdout_map.get_mut(&ca.identifier) {
                            Some(v) => {
                                v.push(stdout_task);
                                v.push(send_stdout_task);
                            }
                            None => {
                                self.stdout_map
                                    .insert(ca.identifier, vec![stdout_task, send_stdout_task]);
                            }
                        }
                        None
                    }
                    ftest_manager::Artifact::Stderr(_) => {
                        panic!("not supported")
                    }
                    ftest_manager::Artifact::Log(log) => match LogStream::from_syslog(log) {
                        Ok(log_stream) => {
                            SuiteEvent::test_case_log(timestamp, name, log_stream).into()
                        }
                        Err(e) => {
                            warn!("Cannot collect logs for test suite: {:?}", e);
                            None
                        }
                    },
                    _ => {
                        panic!("not supported")
                    }
                }
            }
            FidlSuiteEventPayload::SuiteArtifact(sa) => match sa.artifact {
                ftest_manager::Artifact::Stdout(_) => {
                    panic!("not supported")
                }
                ftest_manager::Artifact::Stderr(_) => {
                    panic!("not supported")
                }
                ftest_manager::Artifact::Log(log) => match LogStream::from_syslog(log) {
                    Ok(log_stream) => SuiteEvent::suite_log(timestamp, log_stream).into(),
                    Err(e) => {
                        warn!("Cannot collect logs for test suite: {:?}", e);
                        None
                    }
                },
                _ => {
                    panic!("not supported")
                }
            },
            FidlSuiteEventPayload::SuiteFinished(sf) => SuiteEvent {
                timestamp,
                payload: SuiteEventPayload::RunEvent(RunEvent::SuiteFinished { status: sf.status }),
            }
            .into(),
        };
        if let Some(item) = e {
            sender.send(item).await.context("Cannot send event")?;
        }
        Ok(())
    }
}

#[derive(Debug, thiserror::Error, Eq, PartialEq, Copy, Clone)]
pub enum SuiteLaunchError {
    #[error("Cannot enumerate tests")]
    CaseEnumeration,

    #[error("Cannot resolve test url")]
    InstanceCannotResolve,

    #[error("Invalid arguments passed")]
    InvalidArgs,

    #[error("Cannot connect to test suite")]
    FailedToConnectToTestSuite,

    #[error("resource unavailable")]
    ResourceUnavailable,

    #[error("Some internal error ocurred. Please file bug")]
    InternalError,
}

impl From<ftest_manager::LaunchError> for SuiteLaunchError {
    fn from(error: ftest_manager::LaunchError) -> Self {
        match error {
            ftest_manager::LaunchError::ResourceUnavailable => {
                SuiteLaunchError::ResourceUnavailable
            }
            ftest_manager::LaunchError::InstanceCannotResolve => {
                SuiteLaunchError::InstanceCannotResolve
            }
            ftest_manager::LaunchError::InvalidArgs => SuiteLaunchError::InvalidArgs,
            ftest_manager::LaunchError::FailedToConnectToTestSuite => {
                SuiteLaunchError::FailedToConnectToTestSuite
            }
            ftest_manager::LaunchError::CaseEnumeration => SuiteLaunchError::CaseEnumeration,
            ftest_manager::LaunchError::InternalError => SuiteLaunchError::InternalError,
        }
    }
}

/// Instance to control a single test suite run.
pub struct SuiteRunInstance {
    controller_proxy: Arc<SuiteControllerProxy>,
}

impl SuiteRunInstance {
    pub fn controller(&self) -> Arc<SuiteControllerProxy> {
        self.controller_proxy.clone()
    }

    pub async fn collect_events(&self, sender: mpsc::Sender<SuiteEvent>) -> Result<(), Error> {
        let controller_proxy = self.controller_proxy.clone();
        let mut processor = FidlSuiteEventProcessor::new();
        loop {
            match controller_proxy.get_events().await? {
                Err(e) => return Err(SuiteLaunchError::from(e).into()),
                Ok(events) => {
                    if events.len() == 0 {
                        break;
                    }
                    for event in events {
                        if let Err(e) = processor.process(event, sender.clone()).await {
                            warn!("error running test suite: {:?}", e);
                            let _ = controller_proxy.kill();
                            return Ok(());
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::HandleBased;
    use futures::StreamExt;
    use maplit::hashmap;
    use pretty_assertions::assert_eq;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn collect_test_stdout() {
        let (sock_server, sock_client) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("Failed while creating socket");

        let (sender, mut recv) = mpsc::channel(1);

        let fut = fuchsia_async::Task::spawn(collect_and_send_stdout_internal(LogOpt {
            stdout_socket: sock_client,
            sender: sender.into(),
            buffering_duration: std::time::Duration::from_millis(1),
            buffer_size: LOG_BUFFER_SIZE,
        }));

        sock_server.write(b"test message 1").expect("Can't write msg to socket");
        sock_server.write(b"test message 2").expect("Can't write msg to socket");
        sock_server.write(b"test message 3").expect("Can't write msg to socket");

        let mut msg = recv.next().await;

        assert_eq!(msg, Some("test message 1test message 2test message 3".into()));

        // can receive messages multiple times
        sock_server.write(b"test message 4").expect("Can't write msg to socket");
        msg = recv.next().await;

        assert_eq!(msg, Some("test message 4".into()));

        // messages can be read after socket server is closed.
        sock_server.write(b"test message 5").expect("Can't write msg to socket");
        sock_server.into_handle(); // this will drop this handle and close it.
        fut.await.expect("log collection should not fail");

        msg = recv.next().await;

        assert_eq!(msg, Some("test message 5".into()));

        // socket was closed, this should return None
        msg = recv.next().await;
        assert_eq!(msg, None);
    }

    /// Host side executor doesn't have a fake timer, so these tests only run on device for now.
    #[cfg(target_os = "fuchsia")]
    mod stdout {
        use {
            super::*,
            fidl::endpoints::create_proxy_and_stream,
            fidl_fuchsia_test::CaseListenerMarker,
            fuchsia_async::{pin_mut, TestExecutor},
            fuchsia_zircon::DurationNum,
            matches::assert_matches,
            pretty_assertions::assert_eq,
            std::ops::Add,
        };

        fn send_msg(
            executor: &mut fuchsia_async::TestExecutor,
            log_buffer: &mut StdoutBuffer,
            msg: &str,
        ) {
            let f = async {
                log_buffer.send_log(&msg).await.unwrap();
            };
            pin_mut!(f);
            assert_eq!(executor.run_until_stalled(&mut f), Poll::Ready(()));
        }

        fn recv_msg<T>(
            executor: &mut fuchsia_async::TestExecutor,
            recv: &mut mpsc::Receiver<T>,
        ) -> Poll<Option<T>> {
            let f = recv.next();
            pin_mut!(f);
            executor.run_until_stalled(&mut f)
        }

        #[test]
        fn log_buffer_without_timeout() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sender, mut recv) = mpsc::channel(1);
            let mut log_buffer =
                StdoutBuffer::new(std::time::Duration::from_secs(5), sender.into(), 100);
            let msg1 = "message1".to_owned();
            let msg2 = "message2".to_owned();

            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);
            send_msg(&mut executor, &mut log_buffer, &msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);

            let f = async {
                log_buffer.done().await.unwrap();
            };
            pin_mut!(f);
            assert_eq!(executor.run_until_stalled(&mut f), Poll::Ready(()));
            let mut expected_msg = msg1;
            expected_msg.push_str(&msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(expected_msg)));
        }

        #[test]
        fn log_buffer_with_timeout() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sender, mut recv) = mpsc::channel(1);
            let mut log_buffer =
                StdoutBuffer::new(std::time::Duration::from_secs(5), sender.into(), 100);
            let msg1 = "message1".to_owned();
            let msg2 = "message2".to_owned();

            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);
            send_msg(&mut executor, &mut log_buffer, &msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);

            executor.set_fake_time(executor.now().add(6.seconds()));
            executor.wake_next_timer();
            let mut expected_msg = msg1.clone();
            expected_msg.push_str(&msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(expected_msg)));

            // timer fired, no more buffering should happen.
            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(msg1)));

            send_msg(&mut executor, &mut log_buffer, &msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(msg2)));

            let f = async {
                log_buffer.done().await.unwrap();
            };
            pin_mut!(f);
            assert_eq!(executor.run_until_stalled(&mut f), Poll::Ready(()));
        }

        #[test]
        fn log_buffer_capacity_reached() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sender, mut recv) = mpsc::channel(1);
            let mut log_buffer =
                StdoutBuffer::new(std::time::Duration::from_secs(5), sender.into(), 10);
            let msg1 = "message1".to_owned();
            let msg2 = "message2".to_owned();

            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);
            send_msg(&mut executor, &mut log_buffer, &msg2);
            let mut expected_msg = msg1.clone();
            expected_msg.push_str(&msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(expected_msg)));

            // capacity was reached but buffering is still on, so next msg should buffer
            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);

            let f = async {
                log_buffer.done().await.unwrap();
            };
            pin_mut!(f);
            assert_eq!(executor.run_until_stalled(&mut f), Poll::Ready(()));
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(msg1)));
        }

        #[test]
        fn collect_test_stdout_when_socket_closed() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sock_server, sock_client) = fidl::Socket::create(fidl::SocketOpts::STREAM)
                .expect("Failed while creating socket");

            let (sender, mut recv) = mpsc::channel(1);
            let mut fut = collect_and_send_stdout_internal(LogOpt {
                stdout_socket: sock_client,
                sender: sender.into(),
                buffering_duration: std::time::Duration::from_secs(10),
                buffer_size: LOG_BUFFER_SIZE,
            })
            .boxed();

            sock_server.write(b"test message 1").expect("Can't write msg to socket");
            sock_server.write(b"test message 2").expect("Can't write msg to socket");
            sock_server.write(b"test message 3").expect("Can't write msg to socket");
            sock_server.into_handle(); // this will drop this handle and close it.

            // timer is never fired but we should still receive logs.
            assert_matches!(executor.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
            assert_eq!(
                recv_msg(&mut executor, &mut recv),
                Poll::Ready(Some("test message 1test message 2test message 3".into()))
            );
        }

        #[test]
        fn emit_excessive_runtime_event() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sock_server, sock_client) = fidl::Socket::create(fidl::SocketOpts::STREAM)
                .expect("Failed while creating socket");
            let (listener_proxy, listener_stream) =
                create_proxy_and_stream::<CaseListenerMarker>().expect("Failed to create proxy");

            let name = "test_name";

            let (sender, mut recv) = mpsc::channel(1);
            let mut processor_fut =
                TestCaseProcessor::new(name.to_string(), listener_stream, sock_client, sender)
                    .wait_for_finish()
                    .boxed();

            assert_matches!(executor.run_until_stalled(&mut processor_fut), Poll::Pending);
            assert_matches!(executor.run_until_stalled(&mut recv.next()), Poll::Pending);

            // after LOG_BUFFERING_DURATION seconds log dump timeout is invoked
            let begin_time = executor.now();
            let log_dump_timer = executor.wake_next_timer().expect("Failed to wake next timer");
            executor.set_fake_time(log_dump_timer);
            assert_matches!(executor.run_until_stalled(&mut processor_fut), Poll::Pending);

            // after EXCESSIVE_DURATION the excessive duration event is triggered
            let excessive_duration_timer =
                executor.wake_next_timer().expect("Failed to wake next timer");
            assert_eq!(
                excessive_duration_timer,
                begin_time + fuchsia_zircon::Duration::from(EXCESSIVE_DURATION)
            );
            executor.set_fake_time(excessive_duration_timer);
            assert_matches!(executor.run_until_stalled(&mut processor_fut), Poll::Pending);
            assert_eq!(
                executor.run_until_stalled(&mut recv.next()),
                Poll::Ready(Some(TestEvent::excessive_duration(name, EXCESSIVE_DURATION)))
            );

            // Signal test complete and verify finished event.
            drop(sock_server);
            assert!(listener_proxy
                .finished(fidl_fuchsia_test::Result_ {
                    status: Some(fidl_fuchsia_test::Status::Passed),
                    ..fidl_fuchsia_test::Result_::EMPTY
                })
                .is_ok());
            assert_matches!(executor.run_until_stalled(&mut processor_fut), Poll::Ready(Ok(())));
            assert_eq!(
                executor.run_until_stalled(&mut recv.collect::<Vec<_>>()),
                Poll::Ready(vec![TestEvent::test_case_finished(name, TestResult::Passed)])
            );
        }
    }

    #[test]
    fn group_by_test_case_ordered() {
        let events = vec![
            TestEvent::test_case_started("a::a"),
            TestEvent::test_case_started("b::b"),
            TestEvent::stdout_message("a::a", "log"),
            TestEvent::test_case_started("c::c"),
            TestEvent::stdout_message("b::b", "log"),
            TestEvent::test_case_finished("c::c", TestResult::Passed),
            TestEvent::test_case_finished("a::a", TestResult::Failed),
            TestEvent::test_case_finished("b::b", TestResult::Passed),
            TestEvent::test_finished(),
        ];

        let actual: Vec<(Option<String>, Vec<TestEvent>)> =
            events.into_iter().group_by_test_case_ordered().into_iter().collect();

        let expected = vec![
            (
                Some("a::a".to_string()),
                vec![
                    TestEvent::test_case_started("a::a"),
                    TestEvent::stdout_message("a::a", "log"),
                    TestEvent::test_case_finished("a::a", TestResult::Failed),
                ],
            ),
            (
                Some("b::b".to_string()),
                vec![
                    TestEvent::test_case_started("b::b"),
                    TestEvent::stdout_message("b::b", "log"),
                    TestEvent::test_case_finished("b::b", TestResult::Passed),
                ],
            ),
            (
                Some("c::c".to_string()),
                vec![
                    TestEvent::test_case_started("c::c"),
                    TestEvent::test_case_finished("c::c", TestResult::Passed),
                ],
            ),
            (None, vec![TestEvent::test_finished()]),
        ];

        assert_eq!(actual, expected);
    }

    #[test]
    fn deinterleave() {
        let events = vec![
            TestEvent::test_case_started("a::a"),
            TestEvent::test_case_started("b::b"),
            TestEvent::stdout_message("a::a", "log"),
            TestEvent::test_case_started("c::c"),
            TestEvent::stdout_message("b::b", "log"),
            TestEvent::test_case_finished("c::c", TestResult::Passed),
            TestEvent::test_case_finished("a::a", TestResult::Failed),
            TestEvent::test_case_finished("b::b", TestResult::Passed),
            TestEvent::test_finished(),
        ];

        let expected = vec![
            TestEvent::test_case_started("a::a"),
            TestEvent::stdout_message("a::a", "log"),
            TestEvent::test_case_finished("a::a", TestResult::Failed),
            TestEvent::test_case_started("b::b"),
            TestEvent::stdout_message("b::b", "log"),
            TestEvent::test_case_finished("b::b", TestResult::Passed),
            TestEvent::test_case_started("c::c"),
            TestEvent::test_case_finished("c::c", TestResult::Passed),
            TestEvent::test_finished(),
        ];

        let actual: Vec<TestEvent> = events.into_iter().deinterleave().collect();

        assert_eq!(actual, expected);
    }

    #[test]
    fn group_by_test_case_unordered() {
        let events = vec![
            TestEvent::test_case_started("a::a"),
            TestEvent::test_case_started("b::b"),
            TestEvent::stdout_message("a::a", "log"),
            TestEvent::test_case_started("c::c"),
            TestEvent::stdout_message("b::b", "log"),
            TestEvent::test_case_finished("c::c", TestResult::Passed),
            TestEvent::test_case_finished("a::a", TestResult::Failed),
            TestEvent::test_case_finished("b::b", TestResult::Passed),
            TestEvent::test_finished(),
        ];

        let expected = hashmap! {
            Some("a::a".to_string()) => vec![
                TestEvent::test_case_started("a::a"),
                TestEvent::stdout_message("a::a", "log"),
                TestEvent::test_case_finished("a::a", TestResult::Failed),
            ],
            Some("b::b".to_string()) => vec![
                TestEvent::test_case_started("b::b"),
                TestEvent::stdout_message("b::b", "log"),
                TestEvent::test_case_finished("b::b", TestResult::Passed),
            ],
            Some("c::c".to_string()) => vec![
                TestEvent::test_case_started("c::c"),
                TestEvent::test_case_finished("c::c", TestResult::Passed),
            ],
            None => vec![TestEvent::Finish],
        };

        let actual = events.into_iter().group_by_test_case_unordered();

        assert_eq!(actual, expected)
    }
}
