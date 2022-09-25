// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::manager::DEFAULT_TIMEOUT_IN_SECONDS,
    anyhow::{Context as _, Result},
    async_trait::async_trait,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_diagnostics as diagnostics, fidl_fuchsia_test_manager as test_manager,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::mpsc,
    futures::{join, pin_mut, select, AsyncWriteExt, FutureExt, SinkExt, StreamExt},
    std::cell::RefCell,
    std::collections::LinkedList,
    std::rc::Rc,
    test_manager::Artifact,
    tracing::{info, warn},
};

/// Dispatches `test_manager` artifacts to the `ArtifactBridge`s used to forward data to `ffx fuzz`.
///
/// This object receives the artifacts extracted and forwarded from the `RunEventHandler` and
/// `SuiteEventHandler`.
pub struct ArtifactHandler {
    artifact_receiver: mpsc::UnboundedReceiver<Artifact>,
    stdout: Option<mpsc::UnboundedSender<zx::Socket>>,
    stderr: Option<mpsc::UnboundedSender<zx::Socket>>,
    syslog: Option<mpsc::UnboundedSender<ClientEnd<diagnostics::BatchIteratorMarker>>>,
}

impl ArtifactHandler {
    /// Wraps the artifact channel in a handler.
    pub fn new(artifact_receiver: mpsc::UnboundedReceiver<Artifact>) -> Self {
        Self { artifact_receiver, stdout: None, stderr: None, syslog: None }
    }

    /// Creates an artifact bridge for forwarding stdout from `test_manager` to `ffx fuzz`.
    pub fn create_stdout_bridge(
        &mut self,
        socket_receiver: mpsc::UnboundedReceiver<zx::Socket>,
    ) -> SocketBridge {
        let (artifact_sender, artifact_receiver) = mpsc::unbounded::<zx::Socket>();
        self.stdout = Some(artifact_sender);
        SocketBridge::new(artifact_receiver, socket_receiver)
    }

    /// Creates an artifact bridge for forwarding stderr from `test_manager` to `ffx fuzz`.
    pub fn create_stderr_bridge(
        &mut self,
        socket_receiver: mpsc::UnboundedReceiver<zx::Socket>,
    ) -> SocketBridge {
        let (artifact_sender, artifact_receiver) = mpsc::unbounded::<zx::Socket>();
        self.stderr = Some(artifact_sender);
        SocketBridge::new(artifact_receiver, socket_receiver)
    }

    /// Creates an artifact bridge for forwarding syslog from `test_manager` to `ffx fuzz`.
    pub fn create_syslog_bridge(
        &mut self,
        socket_receiver: mpsc::UnboundedReceiver<zx::Socket>,
    ) -> LogBridge {
        let (artifact_sender, artifact_receiver) =
            mpsc::unbounded::<ClientEnd<diagnostics::BatchIteratorMarker>>();
        self.syslog = Some(artifact_sender);
        LogBridge::new(artifact_receiver, socket_receiver)
    }

    /// Handles `Artifact`s and dispatches them to the appropriate bridge.
    pub async fn run(&mut self) -> Result<()> {
        while let Some(artifact) = self.artifact_receiver.next().await {
            match artifact {
                test_manager::Artifact::Stdout(socket) => {
                    if let Some(mut stdout) = self.stdout.as_ref() {
                        stdout.send(socket).await.context("failed to send stdout artifact")?;
                    }
                }
                test_manager::Artifact::Stderr(socket) => {
                    if let Some(mut stderr) = self.stderr.as_ref() {
                        stderr.send(socket).await.context("failed to send stderr artifact")?;
                    }
                }
                test_manager::Artifact::Log(test_manager::Syslog::Batch(client_end)) => {
                    if let Some(mut syslog) = self.syslog.as_ref() {
                        syslog.send(client_end).await.context("failed to send syslog artifact")?;
                    }
                }
                artifact => info!("Ignoring unsupported artifact: {:?}", artifact),
            };
        }
        Ok(())
    }
}

struct Message {
    data: Vec<u8>,
    expiration: zx::Time,
}

impl Message {
    fn new(data: Vec<u8>) -> Self {
        let mut expiration = zx::Time::get_monotonic();
        expiration += zx::Duration::from_seconds(DEFAULT_TIMEOUT_IN_SECONDS);
        Self { data, expiration }
    }
}

#[async_trait(?Send)]
pub trait ArtifactBridge {
    /// Forwards data from `test_manager` artifacts to `ffx fuzz` sockets.
    async fn forward(&self);
}

#[async_trait(?Send)]
trait ArtifactBridgeInternal: ArtifactBridge {
    // Waits to get an |artifact| for this object.
    async fn get_artifact(&self);

    // Returns bytes read from the registered artifact, or an error if the artifact is unset or the
    // read fails.
    async fn read_from_artifact(&self) -> Vec<u8>;

    /// Returns a future that reads from a received artfact and writes to received sockets.
    ///
    /// This future only completes on error or when the receiver channels are closed.
    async fn forward_impl(&self, socket_receiver: mpsc::UnboundedReceiver<zx::Socket>) {
        // Cap the number of outstanding, unexpired message that can be queued.
        let (msg_sender, msg_receiver) = mpsc::channel::<Message>(0x1000);
        self.get_artifact().await;
        join!(self.recv(msg_sender), self.send(msg_receiver, socket_receiver));
    }

    /// Reads data from the artifact and encapsulate it as timestamped `Message`s.
    async fn recv(&self, mut msg_sender: mpsc::Sender<Message>) {
        loop {
            let data = self.read_from_artifact().await;
            if data.is_empty() {
                break;
            }
            let num_read = data.len();
            if let Err(e) = msg_sender.send(Message::new(data)).await {
                if e.is_full() {
                    warn!("dropped output: {} bytes", num_read);
                }
                if e.is_disconnected() {
                    break;
                }
            }
        }
    }

    /// Reads `Message`s and sends them to sockets as they become available.
    async fn send(
        &self,
        mut msg_receiver: mpsc::Receiver<Message>,
        socket_receiver: mpsc::UnboundedReceiver<zx::Socket>,
    ) {
        let mut socket = None;
        let socket_receiver_rc = Rc::new(RefCell::new(socket_receiver));
        while let Some(msg) = msg_receiver.next().await {
            let now = zx::Time::get_monotonic();
            if msg.expiration < now {
                continue;
            }
            // If there's no socket, try to get one up to the expiration of the current message.
            if socket.is_none() {
                let timer_fut = fasync::Timer::new(msg.expiration - now).fuse();
                let socket_receiver = Rc::clone(&socket_receiver_rc);
                let socket_fut = || async move {
                    let mut socket_receiver = socket_receiver.borrow_mut();
                    socket_receiver.next().await.and_then(|s| fasync::Socket::from_socket(s).ok())
                };
                let socket_fut = socket_fut().fuse();
                pin_mut!(timer_fut, socket_fut);
                socket = select! {
                    _ = timer_fut => None,
                    s = socket_fut => s,
                };
            }
            // Proactively try to replace the socket if one is available. Use a loop to get the most
            // recently provided one.
            loop {
                let mut socket_receiver = socket_receiver_rc.borrow_mut();
                match socket_receiver.try_next() {
                    Ok(Some(s)) => {
                        match fasync::Socket::from_socket(s) {
                            Ok(s) => {
                                socket = Some(s);
                            }
                            Err(e) => {
                                warn!(?e, "failed to convert socket");
                            }
                        };
                    }
                    // Either the socket_receiver closed, or there's no sockets available.
                    Ok(None) => break,
                    Err(_) => break,
                }
            }
            // Clients may reconnect, so if writing to the socket fails, keep reading from the
            // artifact.
            let result = match socket.as_ref() {
                // Only way to get here is to have the msg expire or the socket_recevier close
                // before receiving a usable socket,
                None => continue,
                Some(mut socket) => socket.write_all(&msg.data).await,
            };
            if let Err(e) = result {
                warn!("failed to write to socket: {}", e);
                socket = None;
            }
        }
    }
}

/// Forwards stdio from a persistent socket to a replaceable one.
#[derive(Debug)]
pub struct SocketBridge {
    artifact: RefCell<Option<fasync::Socket>>,
    artifact_receiver: RefCell<mpsc::UnboundedReceiver<zx::Socket>>,
    socket_receiver: RefCell<Option<mpsc::UnboundedReceiver<zx::Socket>>>,
}

impl SocketBridge {
    fn new(
        artifact_receiver: mpsc::UnboundedReceiver<zx::Socket>,
        socket_receiver: mpsc::UnboundedReceiver<zx::Socket>,
    ) -> Self {
        Self {
            artifact: RefCell::new(None),
            artifact_receiver: RefCell::new(artifact_receiver),
            socket_receiver: RefCell::new(Some(socket_receiver)),
        }
    }
}

#[async_trait(?Send)]
impl ArtifactBridge for SocketBridge {
    async fn forward(&self) {
        let socket_receiver = self.socket_receiver.borrow_mut().take().unwrap();
        self.forward_impl(socket_receiver).await
    }
}

#[async_trait(?Send)]
impl ArtifactBridgeInternal for SocketBridge {
    async fn get_artifact(&self) {
        let mut artifact_receiver = self.artifact_receiver.borrow_mut();
        if let Some(socket) = artifact_receiver.next().await {
            let mut artifact = self.artifact.borrow_mut();
            *artifact = fasync::Socket::from_socket(socket).ok();
        }
    }

    async fn read_from_artifact(&self) -> Vec<u8> {
        let mut data = Vec::new();
        let mut artifact = self.artifact.borrow_mut();
        if let Some(socket) = artifact.as_ref() {
            if let Err(e) = socket.read_datagram(&mut data).await {
                if e != zx::Status::PEER_CLOSED {
                    warn!("failed to read from socket: {}", e);
                }
                *artifact = None;
            }
        }
        data
    }
}

/// Forwards JSON-encoded syslogs from a `BatchIterator` client to a replaceable socket.
#[derive(Debug)]
pub struct LogBridge {
    artifact: RefCell<Option<diagnostics::BatchIteratorProxy>>,
    queue: RefCell<LinkedList<diagnostics::FormattedContent>>,
    artifact_receiver:
        RefCell<mpsc::UnboundedReceiver<ClientEnd<diagnostics::BatchIteratorMarker>>>,
    socket_receiver: RefCell<Option<mpsc::UnboundedReceiver<zx::Socket>>>,
}

impl LogBridge {
    fn new(
        artifact_receiver: mpsc::UnboundedReceiver<ClientEnd<diagnostics::BatchIteratorMarker>>,
        socket_receiver: mpsc::UnboundedReceiver<zx::Socket>,
    ) -> Self {
        Self {
            artifact: RefCell::new(None),
            queue: RefCell::new(LinkedList::new()),
            artifact_receiver: RefCell::new(artifact_receiver),
            socket_receiver: RefCell::new(Some(socket_receiver)),
        }
    }
}

#[async_trait(?Send)]
impl ArtifactBridge for LogBridge {
    async fn forward(&self) {
        let socket_receiver = self.socket_receiver.borrow_mut().take().unwrap();
        self.forward_impl(socket_receiver).await
    }
}

#[async_trait(?Send)]
impl ArtifactBridgeInternal for LogBridge {
    async fn get_artifact(&self) {
        let mut artifact_receiver = self.artifact_receiver.borrow_mut();
        if let Some(client_end) = artifact_receiver.next().await {
            let mut artifact = self.artifact.borrow_mut();
            *artifact = client_end.into_proxy().ok();
        }
    }

    async fn read_from_artifact(&self) -> Vec<u8> {
        let mut artifact = self.artifact.borrow_mut();
        let mut queue = self.queue.borrow_mut();
        if let Some(batch_iterator) = artifact.as_ref() {
            if queue.is_empty() {
                match batch_iterator.get_next().await {
                    Ok(Ok(batch)) => queue.extend(batch.into_iter()),
                    Ok(Err(e)) => warn!("BatchIterator returned error: {:?}", e),
                    Err(e) => warn!("BatchIterator FIDL failure: {}", e),
                }
            }
        }
        let buf = match queue.pop_front() {
            Some(diagnostics::FormattedContent::Json(buf)) => buf,
            Some(diagnostics::FormattedContent::Text(buf)) => buf,
            Some(_) => unreachable!("unsupported FormattedContent"),
            None => {
                *artifact = None;
                return Vec::new();
            }
        };
        let mut data = vec![0; buf.size as usize];
        if let Err(e) = buf.vmo.read(&mut data, 0) {
            warn!("failed to read from VMO: {}", e);
        }
        data
    }
}
