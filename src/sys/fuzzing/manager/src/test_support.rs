// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::manager::{FidlEndpoint, Manager},
    anyhow::{Context as _, Error, Result},
    fidl::endpoints::{
        create_proxy, create_proxy_and_stream, create_request_stream, ClientEnd, ControlHandle,
        ServerEnd,
    },
    fidl_fuchsia_diagnostics as fdiagnostics, fidl_fuchsia_fuzzer as fuzz,
    fidl_fuchsia_mem as fmem, fidl_fuchsia_test_manager as test_manager, fuchsia_async as fasync,
    fuchsia_zircon as zx,
    futures::channel::mpsc,
    futures::future::join_all,
    futures::{
        join, pin_mut, select, try_join, AsyncReadExt, AsyncWriteExt, FutureExt, SinkExt,
        StreamExt, TryStreamExt,
    },
    std::cell::RefCell,
    std::collections::HashMap,
    std::rc::Rc,
    test_manager::*,
    zx::HandleBased,
};

////////////////////////////////////////////////////////////////////////////////
// Test fixtures.

const BUF_SIZE: u64 = 4096;

// The |TestRealm| simulates running multiple components concurrently: the |Registry|, the
// test_manager's |RunController| and |SuiteController|, and the |BatchIterator|.

pub struct TestRealm {
    fuzzers: Rc<FakeFuzzerMap>,
    manager_streams: Vec<fuzz::ManagerRequestStream>,
    pub registry_status: zx::Status,
    pub launch_error: Option<LaunchError>,
    pub killable: bool,
}

impl TestRealm {
    pub fn new() -> Self {
        Self {
            fuzzers: Rc::new(FakeFuzzerMap::new()),
            manager_streams: Vec::new(),
            registry_status: zx::Status::OK,
            launch_error: None,
            killable: false,
        }
    }

    pub async fn write_stdout(&self, url: &str, msg: &str) -> Result<()> {
        // Don't hold mutable refs across awaits.
        if let Some(mut socket) = self.fuzzers.take_stdout(url) {
            socket
                .write_all(msg.as_bytes())
                .await
                .context(format!("failed to write to stdout of {}", url))?;
            self.fuzzers.put_stdout(url, socket);
        }
        Ok(())
    }

    pub async fn write_stderr(&self, url: &str, msg: &str) -> Result<()> {
        // Don't hold mutable refs across awaits.
        if let Some(mut socket) = self.fuzzers.take_stderr(url) {
            socket
                .write_all(msg.as_bytes())
                .await
                .context(format!("failed to write to stderr of {}", url))?;
            self.fuzzers.put_stderr(url, socket);
        }
        Ok(())
    }

    pub async fn write_syslog(&self, url: &str, msg: &str) -> Result<()> {
        // Don't hold mutable refs across awaits.
        if let Some(mut sender) = self.fuzzers.take_syslog(url) {
            sender
                .send(msg.to_string())
                .await
                .context(format!("failed to write to syslog of {}", url))?;
            self.fuzzers.put_syslog(url, sender);
        }
        Ok(())
    }

    pub fn send_run_stopped(&self, url: &str) -> Result<()> {
        self.fuzzers
            .send_run_stopped(url)
            .context(format!("failed to send 'run stopped' to {}", url))
    }

    pub fn send_suite_stopped(&self, url: &str) -> Result<()> {
        self.fuzzers
            .send_suite_stopped(url)
            .context(format!("failed to send 'suite stopped' to {}", url))
    }
}

pub fn connect_to_manager(test_realm: Rc<RefCell<TestRealm>>) -> Result<fuzz::ManagerProxy> {
    let (manager_proxy, manager_stream) = create_proxy_and_stream::<fuzz::ManagerMarker>()
        .context("failed to create Manager endpoints")?;
    test_realm.borrow_mut().manager_streams.push(manager_stream);
    Ok(manager_proxy)
}

pub async fn read_async(socket: &zx::Socket) -> Result<String> {
    let socket =
        socket.duplicate_handle(zx::Rights::SAME_RIGHTS).context("failed to duplicate handle")?;
    let mut socket =
        fasync::Socket::from_socket(socket).context("failed to create async socket")?;
    let mut buf: [u8; BUF_SIZE as usize] = [0; BUF_SIZE as usize];
    let bytes_read = socket.read(&mut buf).await.context("failed to read from socket")?;
    Ok(std::str::from_utf8(&buf[..bytes_read]).unwrap_or_default().to_string())
}

pub async fn serve_test_realm(test_realm: Rc<RefCell<TestRealm>>) -> Result<()> {
    // Create a fake registry that serves requests from the fuzz-manager.
    let (registry_proxy, registry_stream) = create_proxy_and_stream::<fuzz::RegistryMarker>()
        .context("failed to create Registry endpoints")?;
    let registry_fut = serve_registry(registry_stream, Rc::clone(&test_realm)).fuse();

    // Create a fake test_manager that serves requests from the fuzz-manager.
    let test_manager = Rc::new(FakeTestManager::new());
    let run_builder = test_manager.create_run_builder();
    let test_manager_fut = test_manager.serve(Rc::clone(&test_realm)).fuse();

    // Multiplex concurrent streams for the manager.
    let (sender, receiver) = mpsc::unbounded::<fuzz::ManagerRequest>();
    let manager_streams = {
        let mut test_realm = test_realm.borrow_mut();
        test_realm.manager_streams.drain(..).collect::<Vec<_>>()
    };
    let mut stream_futs = Vec::new();
    for stream in manager_streams.into_iter() {
        let sender = sender.clone();
        let stream_fut = async move {
            stream.map_err(Error::msg).forward(sender.sink_map_err(Error::msg)).await
        };
        stream_futs.push(stream_fut);
    }
    drop(sender);
    // Create a fake fuzz-manager that serves requests from connected unit tests.
    let fuzz_manager_fut = || async move {
        let manager = Manager::new(registry_proxy, run_builder);
        let results = join!(join_all(stream_futs), manager.serve(receiver));
        for result in results.0.into_iter() {
            result.context("failed to mulitplex stream")?;
        }
        results.1.context("failed to serve manager")?;
        Ok::<(), Error>(())
    };
    let fuzz_manager_fut = fuzz_manager_fut().fuse();

    // The registry and test_manager futures will run indefinitely, so simply drop them once all
    // connections from unit tests to the fuzz-manager have closed.
    pin_mut!(registry_fut, test_manager_fut, fuzz_manager_fut);
    loop {
        select! {
            result = fuzz_manager_fut => break result.context("failed to serve manager"),
            result = registry_fut => result.context("failed to serve fake registry"),
            _ = test_manager_fut => Ok(()),
        }?;
    }
}

// The fake |Registry| tracks which fuzzers have been dis/connected, and allows tests to simulate
// errors by setting the status returned by |fuchsia.fuzzer.Registry.Connect|.
async fn serve_registry(
    stream: fuzz::RegistryRequestStream,
    test_realm: Rc<RefCell<TestRealm>>,
) -> Result<()> {
    let (fuzzers, status) = {
        let test_realm = test_realm.borrow();
        (Rc::clone(&test_realm.fuzzers), test_realm.registry_status)
    };
    stream
        .map(|request| request.context("failed request"))
        .try_for_each(|request| async {
            match request {
                fuzz::RegistryRequest::Connect {
                    fuzzer_url,
                    controller,
                    timeout: _,
                    responder,
                } => {
                    match status {
                        zx::Status::OK => fuzzers.connect(&fuzzer_url, controller),
                        _ => {
                            fuzzers.remove(&fuzzer_url);
                        }
                    };
                    responder.send(status.into_raw())
                }
                fuzz::RegistryRequest::Disconnect { fuzzer_url, responder } => {
                    let status = fuzzers.remove(&fuzzer_url);
                    responder.send(status.into_raw())
                }
            }
            .map_err(Error::msg)
            .context("failed response")
        })
        .await
        .context("failed to serve fake Registry")?;
    Ok(())
}

// The |FakeTestManager| simulates the behavior of test_manager and provides a way to
// |serve_run_builder| streams by connecting them via |FakeRunBuilderEndpoint|s.
struct FakeTestManager {
    sender: mpsc::UnboundedSender<ServerEnd<RunBuilderMarker>>,
    receiver: RefCell<Option<mpsc::UnboundedReceiver<ServerEnd<RunBuilderMarker>>>>,
}

impl FakeTestManager {
    fn new() -> Self {
        let (sender, receiver) = mpsc::unbounded::<ServerEnd<RunBuilderMarker>>();
        Self { sender: sender, receiver: RefCell::new(Some(receiver)) }
    }

    fn create_run_builder(&self) -> FakeRunBuilderEndpoint {
        FakeRunBuilderEndpoint::new(self.sender.clone())
    }

    async fn serve(&self, test_realm: Rc<RefCell<TestRealm>>) {
        const MAX_CONCURRENT: usize = 100;
        let receiver = self.receiver.borrow_mut().take().unwrap();
        receiver
            .for_each_concurrent(MAX_CONCURRENT, |server_end| async {
                let stream = server_end.into_stream().expect("failed to create stream");
                serve_run_builder(stream, Rc::clone(&test_realm))
                    .await
                    .expect("failed to serve run builder");
            })
            .await;
    }
}

// The |FakeRunBuilderEndpoint| provides a way to connect a proxy to a stream served by
// |serve_run_builder|.
struct FakeRunBuilderEndpoint {
    sender: mpsc::UnboundedSender<ServerEnd<RunBuilderMarker>>,
}

impl FakeRunBuilderEndpoint {
    fn new(sender: mpsc::UnboundedSender<ServerEnd<RunBuilderMarker>>) -> Self {
        Self { sender }
    }
}

impl FidlEndpoint<RunBuilderMarker> for FakeRunBuilderEndpoint {
    fn create_proxy(&self) -> Result<RunBuilderProxy> {
        let (proxy, server_end) = create_proxy::<RunBuilderMarker>()?;
        self.sender.unbounded_send(server_end)?;
        Ok(proxy)
    }
}

// Serves |fuchsia.test_manager.RunBuilder| while allowing test realms to configure various errors
// and/or events retrieved by the |Suite-| and |RunController|s.
async fn serve_run_builder(
    mut stream: RunBuilderRequestStream,
    test_realm: Rc<RefCell<TestRealm>>,
) -> Result<()> {
    let (fuzzers, launch_error, killable) = {
        let mut test_realm = test_realm.borrow_mut();
        (Rc::clone(&test_realm.fuzzers), test_realm.launch_error.take(), test_realm.killable)
    };
    // |RunBuilder| requests will always follow a specific order: |AddSuite|, then |Build|.
    let (url, suite_stream) = match stream.next().await {
        Some(Ok(RunBuilderRequest::AddSuite {
            test_url,
            options: _,
            controller,
            control_handle: _,
        })) => {
            let suite_stream =
                controller.into_stream().context("invalid suite controller stream")?;
            (test_url, suite_stream)
        }
        _ => unreachable!(),
    };

    let run_stream = match stream.next().await {
        Some(Ok(RunBuilderRequest::Build { controller, control_handle: _ })) => {
            let run_stream = controller.into_stream().context("invalid run controller stream")?;
            run_stream
        }
        _ => unreachable!(),
    };

    // Tests with launch errors do not have a working syslog and must not start the batch iterator.
    let (batch_client, batch_stream, syslog_sender, syslog_receiver) = match launch_error {
        Some(_) => (None, None, None, None),
        None => {
            fuzzers.launch(&url);
            let (batch_client, batch_stream) =
                create_request_stream::<fdiagnostics::BatchIteratorMarker>()
                    .context("failed to create fuchsia.diagnostics.BatchIterator stream")?;
            let (syslog_sender, syslog_receiver) = mpsc::unbounded::<String>();
            (Some(batch_client), Some(batch_stream), Some(syslog_sender), Some(syslog_receiver))
        }
    };
    let run_receiver = send_default_run_events(Rc::clone(&fuzzers), &url)
        .context("failed to send default run events")?;
    let suite_receiver =
        send_default_suite_events(Rc::clone(&fuzzers), &url, batch_client, syslog_sender)
            .context("failed to send default suite events")?;

    try_join!(
        async {
            serve_suite_controller(suite_stream, launch_error, suite_receiver, killable)
                .await
                .context("failed to serve suite controller")
        },
        async {
            serve_run_controller(run_stream, run_receiver, killable)
                .await
                .context("failed to serve run controller")
        },
        async {
            serve_batch_iterator(batch_stream, syslog_receiver, killable)
                .await
                .context("failed to serve batch iterator")
        },
    )?;
    Ok(())
}

fn send_default_run_events(
    fuzzers: Rc<FakeFuzzerMap>,
    url: &str,
) -> Result<mpsc::UnboundedReceiver<RunEventPayload>> {
    let (sender, receiver) = mpsc::unbounded::<RunEventPayload>();
    fuzzers.put_run_sender(url, sender);
    fuzzers
        .send_run_event(url, RunEventPayload::RunStarted(RunStarted {}))
        .context("failed to send RunStarted run event")?;

    let (out_rx, out_tx) =
        zx::Socket::create(zx::SocketOpts::empty()).context("failed to create socket")?;
    let stdout = fasync::Socket::from_socket(out_tx).context("failed to create async socket")?;
    fuzzers.put_stdout(&url, stdout);
    fuzzers
        .send_run_event(url, RunEventPayload::Artifact(Artifact::Stdout(out_rx)))
        .context("failed to send Stdout run event")?;

    let (err_rx, err_tx) =
        zx::Socket::create(zx::SocketOpts::empty()).context("failed to create socket")?;
    let stderr = fasync::Socket::from_socket(err_tx).context("failed to create async socket")?;
    fuzzers.put_stderr(&url, stderr);
    fuzzers
        .send_run_event(url, RunEventPayload::Artifact(Artifact::Stderr(err_rx)))
        .context("failed to send Stderr run event")?;

    Ok(receiver)
}

fn send_default_suite_events(
    fuzzers: Rc<FakeFuzzerMap>,
    url: &str,
    client_end: Option<ClientEnd<fdiagnostics::BatchIteratorMarker>>,
    syslog_sender: Option<mpsc::UnboundedSender<String>>,
) -> Result<mpsc::UnboundedReceiver<SuiteEventPayload>> {
    let (sender, receiver) = mpsc::unbounded::<SuiteEventPayload>();
    fuzzers.put_suite_sender(url, sender);
    fuzzers
        .send_suite_event(url, SuiteEventPayload::SuiteStarted(SuiteStarted {}))
        .context("failed to send SuiteStarted suite event")?;

    if let Some(syslog_sender) = syslog_sender {
        fuzzers.put_syslog(url, syslog_sender);
    }
    if let Some(client_end) = client_end {
        fuzzers
            .send_suite_event(
                url,
                SuiteEventPayload::SuiteArtifact(SuiteArtifact {
                    artifact: Artifact::Log(Syslog::Batch(client_end)),
                }),
            )
            .context("failed to send Syslog suite event")?;
    };

    let id = fuzzers.get_id(&url);
    fuzzers
        .send_suite_event(
            url,
            SuiteEventPayload::CaseFound(CaseFound {
                test_case_name: "test-case".to_string(),
                identifier: id,
            }),
        )
        .context("failed to send CaseFound suite event")?;
    fuzzers
        .send_suite_event(url, SuiteEventPayload::CaseStarted(CaseStarted { identifier: id }))
        .context("failed to send CaseStarted suite event")?;

    Ok(receiver)
}

async fn serve_suite_controller(
    stream: SuiteControllerRequestStream,
    launch_error: Option<LaunchError>,
    payload_receiver: mpsc::UnboundedReceiver<SuiteEventPayload>,
    killable: bool,
) -> Result<()> {
    let payload_receiver_rc = RefCell::new(payload_receiver);
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            match request {
                SuiteControllerRequest::GetEvents { responder } => {
                    let mut payload_receiver = payload_receiver_rc.borrow_mut();
                    let mut response = match launch_error {
                        Some(e) => Err(e),
                        None => {
                            let mut payloads = Vec::new();
                            while let Ok(Some(payload)) = payload_receiver.try_next() {
                                payloads.push(payload);
                            }
                            if payloads.is_empty() {
                                if let Some(payload) = payload_receiver.next().await {
                                    payloads.push(payload);
                                }
                            }
                            let suite_events = payloads
                                .drain(..)
                                .map(|payload| SuiteEvent {
                                    timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                                    payload: Some(payload),
                                    ..SuiteEvent::EMPTY
                                })
                                .collect();
                            Ok(suite_events)
                        }
                    };
                    // It's okay for the unit test to give up before reading a response.
                    match responder.send(&mut response) {
                        Err(fidl::Error::ServerResponseWrite(status))
                            if killable && status == zx::Status::PEER_CLOSED =>
                        {
                            Ok(())
                        }
                        other => other,
                    }
                }
                _ => unreachable!(),
            }
            .map_err(Error::msg)
            .context("failed response")
        })
        .await
        .context("failed to serve fake SuiteController")?;
    Ok(())
}

async fn serve_run_controller(
    stream: RunControllerRequestStream,
    payload_receiver: mpsc::UnboundedReceiver<RunEventPayload>,
    killable: bool,
) -> Result<()> {
    let payload_receiver_rc = RefCell::new(payload_receiver);
    stream
        .map(|request| request.context("failed request"))
        .try_for_each(|request| async {
            match request {
                RunControllerRequest::GetEvents { responder } => {
                    let mut payload_receiver = payload_receiver_rc.borrow_mut();
                    let mut payloads = Vec::new();
                    while let Ok(Some(payload)) = payload_receiver.try_next() {
                        payloads.push(payload);
                    }
                    if payloads.is_empty() {
                        if let Some(payload) = payload_receiver.next().await {
                            payloads.push(payload);
                        }
                    }
                    let run_events: Vec<RunEvent> = payloads
                        .drain(..)
                        .map(|payload| RunEvent {
                            timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                            payload: Some(payload),
                            ..RunEvent::EMPTY
                        })
                        .collect();
                    let mut response = run_events.into_iter();
                    match responder.send(&mut response) {
                        Err(fidl::Error::ServerResponseWrite(status))
                            if killable && status == zx::Status::PEER_CLOSED =>
                        {
                            Ok(())
                        }
                        other => other,
                    }
                }
                RunControllerRequest::Kill { control_handle } => {
                    control_handle.shutdown();
                    Ok(())
                }
                _ => unreachable!(),
            }
            .map_err(Error::msg)
            .context("failed response")
        })
        .await
        .context("failed to serve fake RunController")?;
    Ok(())
}

async fn serve_batch_iterator(
    stream: Option<fdiagnostics::BatchIteratorRequestStream>,
    receiver: Option<mpsc::UnboundedReceiver<String>>,
    killable: bool,
) -> Result<()> {
    let stream = match stream {
        Some(stream) => stream,
        None => return Ok(()),
    };
    let receiver = match receiver {
        Some(receiver) => RefCell::new(receiver),
        None => return Ok(()),
    };
    stream
        .map(|request| request.context("failed request"))
        .try_for_each(|request| async {
            match request {
                fdiagnostics::BatchIteratorRequest::GetNext { responder } => {
                    let mut receiver = receiver.borrow_mut();
                    let mut logs = Vec::new();
                    // Collect any outstanding logs.
                    loop {
                        match receiver.try_next() {
                            Ok(Some(msg)) => logs.push(msg),
                            Ok(None) => break,
                            Err(_) => break,
                        };
                    }
                    // If no logs, "hang the get" and wait for a log or channel closure.
                    if logs.is_empty() {
                        match receiver.next().await {
                            Some(msg) => logs.push(msg),
                            None => {}
                        };
                    }
                    // Convert the logs.
                    let mut batch = Vec::new();
                    for msg in logs.drain(..) {
                        let vmo = zx::Vmo::create(BUF_SIZE).context("failed to create VMO")?;
                        let size = msg.len() as u64;
                        let buf = fmem::Buffer { vmo, size };
                        buf.vmo.write(msg.as_bytes(), 0).context("failed to write to VMO")?;
                        batch.push(fdiagnostics::FormattedContent::Text(buf));
                    }
                    let mut response = Ok(batch);
                    match responder.send(&mut response) {
                        Err(fidl::Error::ServerResponseWrite(status))
                            if killable && status == zx::Status::PEER_CLOSED =>
                        {
                            Ok(())
                        }
                        other => other,
                    }
                }
            }
            .map_err(Error::msg)
            .context("failed response")
        })
        .await
        .context("failed to serve fake BatchIterator")?;
    Ok(())
}

// The |FakeFuzzerMap| is simply a |HashMap| that can auto-assign test case identifiers. It holds
// the controller stream to prevent it from going out of scope. It also has the producer ends of the
// diagnostic streams, which test can use to produce fake messages. Finally, it has a oneshot
// channel that it triggers on removal by the fake registry, and that notifies the fake run_builder
// to stop the fake run and suite controllers.

#[derive(Default, Debug)]
struct FakeFuzzer {
    id: u32,
    _controller: Option<ServerEnd<fuzz::ControllerMarker>>,
    stdout: Option<fasync::Socket>,
    stderr: Option<fasync::Socket>,
    syslog: Option<mpsc::UnboundedSender<String>>,
    run_sender: Option<mpsc::UnboundedSender<RunEventPayload>>,
    suite_sender: Option<mpsc::UnboundedSender<SuiteEventPayload>>,
}

impl FakeFuzzer {
    fn send_run_event(&mut self, payload: RunEventPayload) -> Result<()> {
        // let stop = match payload {
        //     RunEventPayload::RunStopped(_) => true,
        //     _ => false,
        // };
        if let Some(sender) = self.run_sender.as_ref() {
            sender.unbounded_send(payload).context("failed to send RunEventPayload")?;
            // if !stop {
            //     self.run_sender = Some(sender);
            // }
        }
        Ok(())
    }

    fn send_run_stopped(&mut self) -> Result<()> {
        self.send_run_event(RunEventPayload::RunStopped(RunStopped {}))
            .context("failed to send RunStopped")?;
        self.run_sender = None;
        self.stdout = None;
        self.stderr = None;
        Ok(())
    }

    fn send_suite_event(&mut self, payload: SuiteEventPayload) -> Result<()> {
        if let Some(sender) = self.suite_sender.as_ref() {
            sender.unbounded_send(payload).context("failed to send SuiteEventPayload")?;
        }
        Ok(())
    }

    fn send_suite_stopped(&mut self) -> Result<()> {
        self.send_suite_event(SuiteEventPayload::SuiteStopped(SuiteStopped {
            status: SuiteStatus::Passed,
        }))
        .context("failed to send SuiteStopped event")?;
        self.suite_sender = None;
        self.syslog = None;
        Ok(())
    }
}

#[derive(Debug)]
struct FakeFuzzerMap {
    next_id: RefCell<u32>,
    fuzzers: RefCell<HashMap<String, FakeFuzzer>>,
}

impl FakeFuzzerMap {
    fn new() -> Self {
        Self { next_id: RefCell::new(1), fuzzers: RefCell::new(HashMap::new()) }
    }

    // Adds an entry to the fuzzer map for |url| if it does not exist. Does nothing if |url| is
    // already present in the map.
    fn launch(&self, url: &str) {
        let mut id = self.next_id.borrow_mut();
        let mut fuzzers = self.fuzzers.borrow_mut();
        if fuzzers.contains_key(url) {
            return;
        }
        fuzzers.insert(
            url.to_string(),
            FakeFuzzer {
                id: *id,
                _controller: None,
                stdout: None,
                stderr: None,
                syslog: None,
                run_sender: None,
                suite_sender: None,
            },
        );
        *id += 1;
    }

    fn get_id(&self, url: &str) -> u32 {
        self.fuzzers.borrow_mut().get_mut(url).and_then(|fuzzer| Some(fuzzer.id)).unwrap_or(0)
    }

    fn connect(&self, url: &str, controller: ServerEnd<fuzz::ControllerMarker>) {
        self.fuzzers.borrow_mut().get_mut(url).unwrap()._controller = Some(controller);
    }

    fn put_run_sender(&self, url: &str, run_sender: mpsc::UnboundedSender<RunEventPayload>) {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.run_sender = Some(run_sender);
        }
    }

    fn send_run_event(&self, url: &str, payload: RunEventPayload) -> Result<()> {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.send_run_event(payload).context("failed to send run event")?;
        }
        Ok(())
    }

    fn send_run_stopped(&self, url: &str) -> Result<()> {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.send_run_stopped().context("failed to send 'run stopped'")?;
        }
        Ok(())
    }

    fn put_suite_sender(&self, url: &str, suite_sender: mpsc::UnboundedSender<SuiteEventPayload>) {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.suite_sender = Some(suite_sender);
        }
    }

    fn send_suite_event(&self, url: &str, payload: SuiteEventPayload) -> Result<()> {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.send_suite_event(payload).context("failed to send suite event")?;
        }
        Ok(())
    }

    fn send_suite_stopped(&self, url: &str) -> Result<()> {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.send_suite_stopped().context("failed to send 'suite stopped'")?;
        }
        Ok(())
    }

    fn put_stdout(&self, url: &str, stdout: fasync::Socket) {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.stdout = Some(stdout);
        }
    }

    fn take_stdout(&self, url: &str) -> Option<fasync::Socket> {
        self.fuzzers.borrow_mut().get_mut(url).and_then(|fuzzer| fuzzer.stdout.take())
    }

    fn put_stderr(&self, url: &str, stderr: fasync::Socket) {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.stderr = Some(stderr);
        }
    }

    fn take_stderr(&self, url: &str) -> Option<fasync::Socket> {
        self.fuzzers.borrow_mut().get_mut(url).and_then(|fuzzer| fuzzer.stderr.take())
    }

    fn put_syslog(&self, url: &str, syslog: mpsc::UnboundedSender<String>) {
        let mut fuzzers = self.fuzzers.borrow_mut();
        if let Some(fuzzer) = fuzzers.get_mut(url) {
            fuzzer.syslog = Some(syslog);
        }
    }

    fn take_syslog(&self, url: &str) -> Option<mpsc::UnboundedSender<String>> {
        self.fuzzers.borrow_mut().get_mut(url).and_then(|fuzzer| fuzzer.syslog.take())
    }

    fn remove(&self, url: &str) -> zx::Status {
        let mut fuzzers = self.fuzzers.borrow_mut();
        match fuzzers.remove(url) {
            Some(mut fuzzer) => {
                let _ = fuzzer.send_suite_stopped();
                let _ = fuzzer.send_run_stopped();
                zx::Status::OK
            }
            None => zx::Status::NOT_FOUND,
        }
    }
}
