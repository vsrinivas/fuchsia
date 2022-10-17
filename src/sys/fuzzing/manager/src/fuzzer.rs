// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{ArtifactBridge, ArtifactHandler},
    crate::events::{handle_run_events, handle_suite_events},
    anyhow::{Context as _, Result},
    fidl_fuchsia_fuzzer as fuzz, fidl_fuchsia_test_manager as test_manager,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::{mpsc, oneshot},
    futures::{pin_mut, select, FutureExt, SinkExt},
    std::cell::RefCell,
    std::rc::Rc,
    test_manager::{Artifact, LaunchError, RunControllerProxy, SuiteControllerProxy},
    tracing::warn,
};

/// Represents the what stage of its lifecycle a fuzzer is currently in.
#[derive(Clone, Debug)]
pub enum FuzzerState {
    Stopped,
    Starting,
    Running,
    Failed(zx::Status),
}

/// Represents a fuzzer running on a target device.
#[derive(Debug)]
pub struct Fuzzer {
    state: Rc<RefCell<FuzzerState>>,
    stdout: Option<mpsc::UnboundedSender<zx::Socket>>,
    stderr: Option<mpsc::UnboundedSender<zx::Socket>>,
    syslog: Option<mpsc::UnboundedSender<zx::Socket>>,
    kill: Option<oneshot::Sender<()>>,
    task: Option<fasync::Task<()>>,
}

impl Default for Fuzzer {
    fn default() -> Self {
        Self {
            state: Rc::new(RefCell::new(FuzzerState::Stopped)),
            stdout: None,
            stderr: None,
            syslog: None,
            kill: None,
            task: None,
        }
    }
}

impl Fuzzer {
    /// Returns the current fuzzer state.
    pub fn get_state(&self) -> FuzzerState {
        self.state.borrow().clone()
    }

    fn set_state(&self, desired: FuzzerState) {
        let mut state = self.state.borrow_mut();
        *state = desired;
    }

    /// Starts a fuzzer and sets up connections to forward its output.
    ///
    /// Given `RunController` and `SuiteController` proxies to `test_manager`, this method will
    /// create handlers for `RunEvent`s, `SuiteEvent`s, test `Artifact`s, `SocketBridge`s, and
    /// `LogBridge`s and establish the connections between them to forward stdout, stderr, and
    /// syslog from the fuzzer to the appropriate sockets provided by `ffx fuzz` via a call to
    /// `Fuzzer::get_output`.
    pub async fn start(
        &mut self,
        run_proxy: RunControllerProxy,
        suite_proxy: SuiteControllerProxy,
    ) {
        self.set_state(FuzzerState::Starting);

        let (artifact_sender, artifact_receiver) = mpsc::unbounded::<Artifact>();
        let (stdout_tx, stdout_rx) = mpsc::unbounded::<zx::Socket>();
        let (stderr_tx, stderr_rx) = mpsc::unbounded::<zx::Socket>();
        let (syslog_tx, syslog_rx) = mpsc::unbounded::<zx::Socket>();
        let (start_sender, start_receiver) = oneshot::channel::<Result<(), LaunchError>>();
        let (kill_sender, kill_receiver) = oneshot::channel::<()>();

        let state = Rc::clone(&self.state);
        let mut artifact_handler = ArtifactHandler::new(artifact_receiver);
        let stdout = artifact_handler.create_stdout_bridge(stdout_rx);
        let stderr = artifact_handler.create_stderr_bridge(stderr_rx);
        let syslog = artifact_handler.create_syslog_bridge(syslog_rx);

        let task = || async move {
            let run_fut =
                handle_run_events(run_proxy, artifact_sender.clone(), kill_receiver).fuse();
            let suite_fut = handle_suite_events(suite_proxy, artifact_sender, start_sender).fuse();
            let artifact_fut = artifact_handler.run().fuse();
            let stdout_fut = stdout.forward().fuse();
            let stderr_fut = stderr.forward().fuse();
            let syslog_fut = syslog.forward().fuse();
            pin_mut!(run_fut, suite_fut, artifact_fut, stdout_fut, stderr_fut, syslog_fut);
            let mut event_futs = 2;
            while event_futs > 0 {
                let result = select! {
                    result = run_fut => {
                        event_futs -= 1;
                        result
                    }
                    result = suite_fut => {
                        event_futs -= 1;
                        result
                    }
                    result = artifact_fut => result,
                    _ = stdout_fut => Ok(()),
                    _ = stderr_fut => Ok(()),
                    _ = syslog_fut => Ok(()),
                };
                if let Err(e) = result {
                    warn!("{:?}", e);
                    break;
                }
            }
            let mut state = state.borrow_mut();
            *state = FuzzerState::Stopped;
        };

        self.stdout = Some(stdout_tx);
        self.stderr = Some(stderr_tx);
        self.syslog = Some(syslog_tx);
        self.kill = Some(kill_sender);
        self.task = Some(fasync::Task::local(task()));

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

    /// Installs a socket to receive fuzzer output of the given type.
    pub async fn get_output(&mut self, output: fuzz::TestOutput, socket: zx::Socket) -> Result<()> {
        match output {
            fuzz::TestOutput::Stdout => {
                if let Some(mut stdout) = self.stdout.as_ref() {
                    stdout.send(socket).await.context("failed to send stdout")?;
                }
            }
            fuzz::TestOutput::Stderr => {
                if let Some(mut stderr) = self.stderr.as_ref() {
                    stderr.send(socket).await.context("failed to send stderr")?;
                }
            }
            fuzz::TestOutput::Syslog => {
                if let Some(mut syslog) = self.syslog.as_ref() {
                    syslog.send(socket).await.context("failed to send syslog")?;
                }
            }
            _ => unimplemented!(),
        }
        Ok(())
    }

    /// Waits for the fuzzer to stop.
    ///
    /// If a `max_wait_time` is provided, and the fuzzer currently has an output forwarding task, it
    /// will wait up to that much time for the task to complete. If it has a task but no
    /// `max_wait_time` is provided, it will drop the task immediately.
    ///
    /// Returns an error if the `max_wait_time` elapses without the output forwarding task
    /// completing.
    ///
    pub async fn stop(&mut self, max_wait_time: Option<zx::Duration>) -> Result<(), zx::Status> {
        if let (Some(task), Some(max_wait_time)) = (self.task.take(), max_wait_time) {
            // TODO(fxbug.dev/112353): Extend the test fixtures and add tests for the timeout case.
            let stop_fut = task.fuse();
            let timer_fut = fasync::Timer::new(max_wait_time).fuse();
            pin_mut!(stop_fut, timer_fut);
            select! {
                _ = stop_fut => Ok(()),
                _ = timer_fut => Err(zx::Status::TIMED_OUT)
            }?;
        }
        self.stdout = None;
        self.stderr = None;
        self.syslog = None;
        self.set_state(FuzzerState::Stopped);
        Ok(())
    }

    /// Stops the fuzzer immediately.
    pub async fn kill(&mut self) -> Result<(), zx::Status> {
        if let Some(kill) = self.kill.take() {
            let _ = kill.send(());
        }
        self.stop(None).await
    }
}
