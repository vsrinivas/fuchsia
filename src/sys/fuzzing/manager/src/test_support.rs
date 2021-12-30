// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

////////////////////////////////////////////////////////////////////////////////
// Test fixtures.

use {
    fidl::endpoints::{create_proxy_and_stream, create_request_stream, ControlHandle},
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_fuzzer as fuzz,
    fidl_fuchsia_test_manager as test_manager, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::{mpsc, oneshot},
    futures::{
        lock::Mutex, select, AsyncReadExt, AsyncWriteExt, FutureExt, StreamExt, TryStreamExt,
    },
    std::{collections::HashMap, str, sync::Arc},
};

// The artifacts plumb data from |TestManager::append| to the sockets returned via
// |FuzzManager::connect|.
#[derive(Debug)]
pub enum Artifact {
    Stdout(String),
    Stderr(String),
    SyslogInline(String),
    SyslogSocket(String),
}

#[derive(Default)]
pub struct TestManager {
    fuzzers: Arc<Mutex<HashMap<String, Fuzzer>>>,
    next_identifier: u32,
    registry: Option<fasync::Task<()>>,
}

impl TestManager {
    pub fn new() -> Self {
        Self { fuzzers: Arc::new(Mutex::new(HashMap::new())), next_identifier: 0, registry: None }
    }

    pub fn serve_registry(&mut self, status: zx::Status) -> fuzz::RegistryProxy {
        assert!(self.registry.is_none());
        let (proxy, stream) = create_proxy_and_stream::<fuzz::RegistryMarker>()
            .expect("failed to create fuchsia.fuzzer.Registry proxy and/or stream");
        self.registry = Some(serve_registry(stream, status, self.fuzzers.clone()));
        proxy
    }

    pub async fn make_run_builder(
        &mut self,
        fuzzer_url: &str,
        on_launch: Option<test_manager::LaunchError>,
    ) -> test_manager::RunBuilderProxy {
        self.next_identifier += 1;
        let mut fuzzer = Fuzzer::new(self.next_identifier, on_launch);
        let proxy = fuzzer.serve_run_builder(fuzzer_url.to_string(), self.fuzzers.clone());
        {
            let mut fuzzers = self.fuzzers.lock().await;
            fuzzers.insert(fuzzer_url.to_string(), fuzzer);
        }
        proxy
    }

    pub async fn start_suite(&self, fuzzer_url: &str) {
        let mut fuzzers = self.fuzzers.lock().await;
        let fuzzer = fuzzers
            .get_mut(&fuzzer_url.to_string())
            .unwrap_or_else(|| panic!("failed to resolve {}", fuzzer_url));
        fuzzer.start_suite();
    }

    pub async fn start_case(&self, fuzzer_url: &str) {
        let mut fuzzers = self.fuzzers.lock().await;
        let fuzzer = fuzzers
            .get_mut(&fuzzer_url.to_string())
            .unwrap_or_else(|| panic!("failed to resolve {}", fuzzer_url));
        fuzzer.start_case();
    }

    pub async fn append(&self, fuzzer_url: &str, artifact: Artifact) {
        let mut fuzzers = self.fuzzers.lock().await;
        let fuzzer = fuzzers
            .get_mut(&fuzzer_url.to_string())
            .unwrap_or_else(|| panic!("failed to resolve {}", fuzzer_url));
        fuzzer.append(artifact).await;
    }

    pub async fn stop(&self, fuzzer_url: &str) {
        let mut fuzzer = {
            let mut fuzzers = self.fuzzers.lock().await;
            fuzzers
                .remove(&fuzzer_url.to_string())
                .unwrap_or_else(|| panic!("failed to resolve {}", fuzzer_url))
        };
        fuzzer.stop().await;
    }

    pub async fn kill(&self, fuzzer_url: &str) {
        let mut fuzzer = {
            let mut fuzzers = self.fuzzers.lock().await;
            fuzzers
                .remove(&fuzzer_url.to_string())
                .unwrap_or_else(|| panic!("failed to resolve {}", fuzzer_url))
        };
        fuzzer.kill().await;
    }

    pub async fn reset(&self, fuzzer_url: &str) {
        let mut fuzzer = {
            let mut fuzzers = self.fuzzers.lock().await;
            fuzzers
                .remove(&fuzzer_url.to_string())
                .unwrap_or_else(|| panic!("failed to resolve {}", fuzzer_url))
        };
        fuzzer.join().await;
    }

    pub async fn stop_registry(&mut self, mut registry: Option<fuzz::RegistryProxy>) {
        registry.take();
        if let Some(task) = self.registry.take() {
            task.await;
        }
    }
}

struct Fuzzer {
    identifier: u32,
    on_launch: Option<test_manager::LaunchError>,
    event_sender: mpsc::UnboundedSender<test_manager::SuiteEventPayload>,
    event_receiver: Option<mpsc::UnboundedReceiver<test_manager::SuiteEventPayload>>,
    run_builder: Option<fasync::Task<()>>,
    run_controller: Option<fasync::Task<()>>,
    suite_controller: Option<fasync::Task<()>>,
    stdout: Option<zx::Socket>,
    stderr: Option<zx::Socket>,
    syslog: Option<mpsc::UnboundedSender<rc::ArchiveIteratorEntry>>,
    archive: Option<fasync::Task<()>>,
}

impl Fuzzer {
    fn new(identifier: u32, on_launch: Option<test_manager::LaunchError>) -> Self {
        let (sender, receiver) = mpsc::unbounded::<test_manager::SuiteEventPayload>();
        Self {
            identifier: identifier,
            on_launch: on_launch,
            event_sender: sender,
            event_receiver: Some(receiver),
            run_builder: None,
            run_controller: None,
            suite_controller: None,
            stdout: None,
            stderr: None,
            syslog: None,
            archive: None,
        }
    }

    fn serve_run_builder(
        &mut self,
        fuzzer_url: String,
        fuzzers: Arc<Mutex<HashMap<String, Fuzzer>>>,
    ) -> test_manager::RunBuilderProxy {
        let (proxy, stream) = create_proxy_and_stream::<test_manager::RunBuilderMarker>()
            .expect("failed to create fuchsia.test.manager.RunBuilder proxy and/or stream");
        self.run_builder = Some(serve_run_builder(stream, fuzzer_url, fuzzers));
        proxy
    }

    fn serve_suite_controller(
        &mut self,
        stream: test_manager::SuiteControllerRequestStream,
        stop_sender: oneshot::Sender<()>,
    ) {
        assert!(self.event_receiver.is_some());
        let event_receiver = self.event_receiver.take().unwrap();
        self.suite_controller =
            Some(serve_suite_controller(stream, event_receiver, stop_sender, self.on_launch));
    }

    fn serve_run_controller(
        &mut self,
        stream: test_manager::RunControllerRequestStream,
        stop_receiver: oneshot::Receiver<()>,
        fuzzer_url: String,
        fuzzers: Arc<Mutex<HashMap<String, Fuzzer>>>,
    ) {
        self.run_controller =
            Some(serve_run_controller(stream, stop_receiver, fuzzer_url, fuzzers));
    }

    fn send_event(&self, payload: test_manager::SuiteEventPayload) {
        // If the channel has been closed, e.g. by |kill|, just drop the events on the floor.
        let _ = self.event_sender.unbounded_send(payload);
    }

    fn start_suite(&mut self) {
        self.send_event(test_manager::SuiteEventPayload::SuiteStarted(
            test_manager::SuiteStarted {},
        ));
        let (client_end, stream) = create_request_stream::<rc::ArchiveIteratorMarker>()
            .expect("failed to create archive iterator stream");
        let (sender, receiver) = mpsc::unbounded::<rc::ArchiveIteratorEntry>();
        self.syslog = Some(sender);
        self.archive = Some(serve_archive_iterator(stream, receiver));
        self.send_event(test_manager::SuiteEventPayload::SuiteArtifact(
            test_manager::SuiteArtifact {
                artifact: test_manager::Artifact::Log(test_manager::Syslog::Archive(client_end)),
            },
        ));
    }

    fn start_case(&mut self) {
        self.send_event(test_manager::SuiteEventPayload::CaseFound(test_manager::CaseFound {
            test_case_name: self.identifier.to_string(),
            identifier: self.identifier,
        }));
        self.send_event(test_manager::SuiteEventPayload::CaseStarted(test_manager::CaseStarted {
            identifier: self.identifier,
        }));
        let (out_rx, out_tx) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        self.stdout = Some(out_tx);
        self.send_event(test_manager::SuiteEventPayload::CaseArtifact(
            test_manager::CaseArtifact {
                identifier: self.identifier,
                artifact: test_manager::Artifact::Stdout(out_rx),
            },
        ));
        let (err_rx, err_tx) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        self.stderr = Some(err_tx);
        self.send_event(test_manager::SuiteEventPayload::CaseArtifact(
            test_manager::CaseArtifact {
                identifier: self.identifier,
                artifact: test_manager::Artifact::Stderr(err_rx),
            },
        ));
    }

    async fn append(&self, artifact: Artifact) {
        match artifact {
            Artifact::Stdout(line) => {
                let socket = self.stdout.as_ref().expect("stdout not connected");
                socket.write(line.as_bytes()).expect("failed to write to stdout socket");
            }
            Artifact::Stderr(line) => {
                let socket = self.stderr.as_ref().expect("stderr not connected");
                socket.write(line.as_bytes()).expect("failed to write to stderr socket");
            }
            Artifact::SyslogInline(line) => {
                let syslog = self.syslog.as_ref().expect("syslog not connected");
                syslog
                    .unbounded_send(rc::ArchiveIteratorEntry {
                        diagnostics_data: Some(rc::DiagnosticsData::Inline(rc::InlineData {
                            data: line.to_string(),
                            truncated_chars: 0,
                        })),
                        ..rc::ArchiveIteratorEntry::EMPTY
                    })
                    .expect("failed to send ArchiveIteratorEntry");
            }
            Artifact::SyslogSocket(line) => {
                let syslog = self.syslog.as_ref().expect("syslog not connected");
                let (rx, tx) = fuchsia_zircon::Socket::create(zx::SocketOpts::STREAM)
                    .expect("failed to create socket");
                syslog
                    .unbounded_send(rc::ArchiveIteratorEntry {
                        diagnostics_data: Some(rc::DiagnosticsData::Socket(rx)),
                        ..rc::ArchiveIteratorEntry::EMPTY
                    })
                    .expect("failed to send ArchiveIteratorEntry");
                let mut tx =
                    fasync::Socket::from_socket(tx).expect("failed to create async socket");
                let data = line.as_bytes();
                let mut offset = 0;
                while offset < data.len() {
                    let sent =
                        tx.write(&data[offset..]).await.expect("failed to write to syslog socket");
                    offset += sent;
                }
            }
        };
    }

    // This is idempotent, and may be called multiple times. It may NOT be called from the
    // |serve_archive_iterator| tasks.
    async fn disconnect(&mut self) {
        self.stdout.take();
        self.stderr.take();
        if let Some(syslog) = self.syslog.take() {
            syslog.close_channel();
        }
        if let Some(task) = self.archive.take() {
            task.await;
        }
    }

    async fn stop(&mut self) {
        self.send_event(test_manager::SuiteEventPayload::CaseStopped(test_manager::CaseStopped {
            identifier: self.identifier,
            status: test_manager::CaseStatus::Passed,
        }));
        self.disconnect().await;
        self.send_event(test_manager::SuiteEventPayload::CaseFinished(
            test_manager::CaseFinished { identifier: self.identifier },
        ));
        self.send_event(test_manager::SuiteEventPayload::SuiteStopped(
            test_manager::SuiteStopped { status: test_manager::SuiteStatus::Passed },
        ));
        self.join().await;
    }

    async fn kill(&mut self) {
        self.event_sender.close_channel();
        self.disconnect().await;
        // Do not call |join|; as this may be called by |serve_run_controller|. |serve_registry|
        // should always receive a call to |fuchsia.fuzzer.Registry.Disconnect|, which will call
        // |stop| and ensure all tasks are reaped.
    }

    // This is idempotent, and may be called multiple times. It may NOT be called from the
    // |serve_run_builder|, |serve_suite_controller| or |serve_run_controller| tasks.
    async fn join(&mut self) {
        self.disconnect().await;
        let tasks =
            [self.run_builder.take(), self.suite_controller.take(), self.run_controller.take()];
        for task in tasks {
            if let Some(task) = task {
                task.await;
            }
        }
    }
}

#[derive(Debug)]
pub struct Diagnostics {
    socket: fasync::Socket,
}

impl Diagnostics {
    pub fn new(socket: zx::Socket) -> Self {
        Self { socket: fasync::Socket::from_socket(socket).expect("failed to create async-socket") }
    }

    pub async fn next(&mut self) -> anyhow::Result<String> {
        let mut buf: [u8; 4096] = [0; 4096];
        let bytes_read = self.socket.read(&mut buf).await?;
        Ok(str::from_utf8(&buf[..bytes_read]).unwrap_or("").to_string())
    }
}

fn serve_registry(
    stream: fuzz::RegistryRequestStream,
    status: zx::Status,
    fuzzers: Arc<Mutex<HashMap<String, Fuzzer>>>,
) -> fasync::Task<()> {
    fasync::Task::spawn(async move {
        stream
            .try_for_each(|request| async {
                let fuzzers_for_request = fuzzers.clone();
                match request {
                    fuzz::RegistryRequest::Connect {
                        fuzzer_url: _,
                        controller: _,
                        timeout: _,
                        responder,
                    } => responder.send(status.into_raw()),
                    fuzz::RegistryRequest::Disconnect { fuzzer_url, responder } => {
                        let mut fuzzers = fuzzers_for_request.lock().await;
                        let response = match fuzzers.get_mut(&fuzzer_url) {
                            Some(fuzzer) => {
                                fuzzer.stop().await;
                                zx::Status::OK
                            }
                            None => zx::Status::NOT_FOUND,
                        };
                        responder.send(response.into_raw())
                    }
                }
            })
            .await
            .expect("registry error")
    })
}

fn serve_run_builder(
    mut stream: test_manager::RunBuilderRequestStream,
    fuzzer_url: String,
    fuzzers: Arc<Mutex<HashMap<String, Fuzzer>>>,
) -> fasync::Task<()> {
    fasync::Task::spawn(async move {
        let suite_controller_stream = match stream.next().await {
            Some(Ok(test_manager::RunBuilderRequest::AddSuite {
                test_url,
                options: _,
                controller,
                control_handle: _,
            })) => {
                assert_eq!(fuzzer_url, test_url);
                controller.into_stream().expect("invalid suite controller stream")
            }
            _ => unreachable!(),
        };
        let (stop_sender, stop_receiver) = oneshot::channel::<()>();
        {
            let mut fuzzers = fuzzers.lock().await;
            if let Some(fuzzer) = fuzzers.get_mut(&fuzzer_url) {
                fuzzer.serve_suite_controller(suite_controller_stream, stop_sender);
            }
        }
        let run_controller_stream = match stream.next().await {
            Some(Ok(test_manager::RunBuilderRequest::Build { controller, control_handle: _ })) => {
                controller.into_stream().expect("invalid run controller stream")
            }
            _ => unreachable!(),
        };
        let cloned_fuzzers = fuzzers.clone();
        {
            let mut fuzzers = fuzzers.lock().await;
            if let Some(fuzzer) = fuzzers.get_mut(&fuzzer_url) {
                fuzzer.serve_run_controller(
                    run_controller_stream,
                    stop_receiver,
                    fuzzer_url,
                    cloned_fuzzers,
                );
            }
        }
    })
}

fn serve_suite_controller(
    mut stream: test_manager::SuiteControllerRequestStream,
    mut event_receiver: mpsc::UnboundedReceiver<test_manager::SuiteEventPayload>,
    stop_sender: oneshot::Sender<()>,
    on_launch: Option<test_manager::LaunchError>,
) -> fasync::Task<()> {
    let mut pending = match on_launch {
        Some(err) => Some(Err(err)),
        None => None,
    };
    fasync::Task::spawn(async move {
        while let Some(request) = stream.next().await {
            match request.unwrap_or_else(|e| {
                panic!("fuchsia.test.manager.SuiteController received FIDL error: {}", e)
            }) {
                test_manager::SuiteControllerRequest::GetEvents { responder } => {
                    let mut response = match pending.take() {
                        Some(response) => response,
                        None => {
                            let event = event_receiver.next().await;
                            if let Some(test_manager::SuiteEventPayload::SuiteStopped(_)) = event {
                                pending = Some(Ok(Vec::new()));
                            }
                            match event {
                                Some(payload) => Ok(vec![test_manager::SuiteEvent {
                                    timestamp: Some(zx::Time::get_monotonic().into_nanos()),
                                    payload: Some(payload),
                                    ..test_manager::SuiteEvent::EMPTY
                                }]),
                                None => Ok(Vec::new()),
                            }
                        }
                    };
                    let last = match response.as_ref() {
                        Ok(events) => events.is_empty(),
                        Err(_) => true,
                    };
                    responder.send(&mut response).unwrap_or_else(|e| {
                        panic!(
                            "failed to send response for {}: {}",
                            "fuchsia.test.manager.SuiteController.GetEvents", e,
                        );
                    });
                    if last {
                        let _ = stop_sender.send(());
                        break;
                    }
                }
                _ => unreachable!(),
            }
        }
    })
}

fn serve_run_controller(
    stream: test_manager::RunControllerRequestStream,
    stop_receiver: oneshot::Receiver<()>,
    fuzzer_url: String,
    fuzzers: Arc<Mutex<HashMap<String, Fuzzer>>>,
) -> fasync::Task<()> {
    fasync::Task::spawn(async move {
        let mut receive_next = stream.fuse();
        let mut receive_stop = stop_receiver.into_stream().fuse();
        select! {
            next = receive_next.next() => {
                match next {
                    Some(Ok(test_manager::RunControllerRequest::Kill{ control_handle })) => {
                        let mut fuzzers = fuzzers.lock().await;
                        let fuzzer = fuzzers.get_mut(&fuzzer_url).unwrap_or_else(|| {
                            panic!("failed to resolve for {}: {}",
                              "fuchsia.test.manager.RunController", fuzzer_url);
                        });
                        fuzzer.kill().await;
                        control_handle.shutdown();
                    }
                    _ => unreachable!(),
                }
            }
            _ = receive_stop.next() => {},
        };
    })
}

fn serve_archive_iterator(
    mut stream: rc::ArchiveIteratorRequestStream,
    mut receiver: mpsc::UnboundedReceiver<rc::ArchiveIteratorEntry>,
) -> fasync::Task<()> {
    fasync::Task::spawn(async move {
        while let Some(entry) = receiver.next().await {
            match stream.next().await {
                Some(Ok(rc::ArchiveIteratorRequest::GetNext { responder })) => {
                    let mut response = Ok(vec![entry]);
                    responder.send(&mut response).unwrap_or_else(|e| panic!("failed to send response for fuchsia.developer.remotecontrol.ArchiveIterator.GetNext: {}", e));
                }
                Some(Err(e)) => panic!(
                    "fuchsia.developer.remotecontrol.ArchiveIterator received FIDL error: {}",
                    e
                ),
                None => break,
            }
        }
    })
}
