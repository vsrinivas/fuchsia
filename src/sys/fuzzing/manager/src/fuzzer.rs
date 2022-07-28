// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{forward_all, SocketTrio},
    crate::events::{handle_run_events, handle_suite_events, LaunchResult},
    anyhow::{anyhow, Context as _, Error, Result},
    fidl_fuchsia_test_manager as test_manager, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::{mpsc, oneshot},
    futures::{join, SinkExt},
    std::cell::RefCell,
    std::rc::Rc,
    test_manager::{Artifact, LaunchError, RunControllerProxy, SuiteControllerProxy},
    tracing::warn,
};

pub type ConnectResponse = std::result::Result<(zx::Socket, zx::Socket, zx::Socket), i32>;

#[derive(Clone, Debug)]
pub enum FuzzerState {
    Stopped,
    Starting,
    Running,
    Failed(zx::Status),
}

#[derive(Debug)]
pub struct Fuzzer {
    state: Rc<RefCell<FuzzerState>>,
    sender: Option<mpsc::UnboundedSender<SocketTrio>>,
    kill: Option<oneshot::Sender<()>>,
    task: Option<fasync::Task<()>>,
}

impl Default for Fuzzer {
    fn default() -> Self {
        Self {
            state: Rc::new(RefCell::new(FuzzerState::Stopped)),
            sender: None,
            kill: None,
            task: None,
        }
    }
}

impl Fuzzer {
    pub fn get_state(&self) -> FuzzerState {
        self.state.borrow().clone()
    }

    fn set_state(&self, desired: FuzzerState) {
        let mut state = self.state.borrow_mut();
        *state = desired;
    }

    pub async fn start(
        &mut self,
        run_proxy: RunControllerProxy,
        suite_proxy: SuiteControllerProxy,
    ) {
        self.set_state(FuzzerState::Starting);

        // Start a task to handle feedback from the test.
        let (sockets_sender, sockets_receiver) = mpsc::unbounded::<SocketTrio>();
        let (start_sender, start_receiver) = oneshot::channel::<Result<(), LaunchError>>();
        let (kill_sender, kill_receiver) = oneshot::channel::<()>();
        self.sender = Some(sockets_sender);
        self.kill = Some(kill_sender);
        self.task = Some(fasync::Task::local(run_fuzzer(
            Rc::clone(&self.state),
            run_proxy,
            suite_proxy,
            sockets_receiver,
            start_sender,
            kill_receiver,
        )));

        // Wait for the task to indicate it has launched (or failed).
        let launch_result = match start_receiver.await {
            Ok(launch_result) => launch_result,
            Err(_) => {
                warn!("failed to start fuzzer: suite controller closed unexpectedly");
                self.set_state(FuzzerState::Failed(zx::Status::INTERNAL));
                return;
            }
        };
        match launch_result {
            Ok(()) => self.set_state(FuzzerState::Running),
            Err(e) => {
                warn!("failed to start fuzzer: {:?}", e);
                let status = match e {
                    LaunchError::ResourceUnavailable => zx::Status::NO_RESOURCES,
                    LaunchError::InstanceCannotResolve => zx::Status::NOT_FOUND,
                    LaunchError::InvalidArgs => zx::Status::INVALID_ARGS,
                    LaunchError::FailedToConnectToTestSuite => zx::Status::NOT_SUPPORTED,
                    LaunchError::NoMatchingCases => zx::Status::NOT_FOUND,
                    _ => zx::Status::INTERNAL,
                };
                self.set_state(FuzzerState::Failed(status));
            }
        };
    }

    pub async fn connect(&mut self) -> Result<Result<(zx::Socket, zx::Socket, zx::Socket), i32>> {
        let (rx, tx) = SocketTrio::create().context("failed to create sockets")?;
        let response = match self.sender.as_ref() {
            Some(mut sender) => {
                sender.send(tx).await.context("failed to send sockets")?;
                Ok((rx.stdout, rx.stderr, rx.syslog))
            }
            None => {
                warn!("failed to connect fuzzer: shutting down...");
                Err(zx::Status::BAD_STATE.into_raw())
            }
        };
        Ok(response)
    }

    pub async fn stop(&mut self) {
        if let Some(task) = self.task.take() {
            task.await;
        }
        self.sender = None;
        self.set_state(FuzzerState::Stopped);
    }

    pub async fn kill(&mut self) {
        if let Some(kill) = self.kill.take() {
            let _ = kill.send(());
        }
        self.stop().await;
    }
}

async fn run_fuzzer(
    state: Rc<RefCell<FuzzerState>>,
    run_proxy: RunControllerProxy,
    suite_proxy: SuiteControllerProxy,
    sockets_receiver: mpsc::UnboundedReceiver<SocketTrio>,
    start_sender: oneshot::Sender<LaunchResult>,
    kill_receiver: oneshot::Receiver<()>,
) {
    let (artifact_sender, artifact_receiver) = mpsc::unbounded::<Artifact>();
    let (stop_sender, stop_receiver) = oneshot::channel::<()>();
    let events_fut = async move {
        let results = join!(
            handle_run_events(run_proxy, artifact_sender.clone(), kill_receiver),
            handle_suite_events(suite_proxy, artifact_sender, start_sender),
        );
        results.0?;
        results.1?;
        stop_sender.send(()).map_err(|_| anyhow!("failed to stop output forwarding"))?;
        Ok::<(), Error>(())
    };
    let results =
        join!(events_fut, forward_all(artifact_receiver, sockets_receiver, stop_receiver),);
    {
        let mut state = state.borrow_mut();
        *state = FuzzerState::Stopped;
    }
    if let Err(e) = results.0 {
        warn!("{:?}", e);
    }
    if let Err(e) = results.1 {
        warn!("{:?}", e);
    }
}
