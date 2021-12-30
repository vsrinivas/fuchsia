// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_test_manager as test_manager,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    fuchsia_zircon as zx,
    futures::channel::{mpsc, oneshot},
};

#[derive(Debug, PartialEq, Copy, Clone)]
struct DiagnosticFlags {
    has_stdout: bool,
    has_stderr: bool,
    has_syslog: bool,
}

impl DiagnosticFlags {
    fn all() -> Self {
        Self { has_stdout: true, has_stderr: true, has_syslog: true }
    }

    fn none() -> Self {
        Self { has_stdout: false, has_stderr: false, has_syslog: false }
    }
}

#[derive(Debug, PartialEq, Copy, Clone)]
enum SuiteState {
    Initial(DiagnosticFlags),
    Started(DiagnosticFlags),
    Ready(DiagnosticFlags),
    Stopped,
}

#[derive(Debug)]
pub struct SuiteEventHandler {
    out_sink: mpsc::UnboundedSender<zx::Socket>,
    err_sink: mpsc::UnboundedSender<zx::Socket>,
    log_sink: mpsc::UnboundedSender<ClientEnd<rc::ArchiveIteratorMarker>>,
    state: SuiteState,
}

impl SuiteEventHandler {
    pub fn new(
        out_sink: mpsc::UnboundedSender<zx::Socket>,
        err_sink: mpsc::UnboundedSender<zx::Socket>,
        log_sink: mpsc::UnboundedSender<ClientEnd<rc::ArchiveIteratorMarker>>,
    ) -> Self {
        Self {
            out_sink: out_sink,
            err_sink: err_sink,
            log_sink: log_sink,
            state: SuiteState::Initial(DiagnosticFlags::none()),
        }
    }

    pub async fn handle(
        &mut self,
        suite: test_manager::SuiteControllerProxy,
        sender: oneshot::Sender<zx::Status>,
    ) -> Result<(), zx::Status> {
        let status = zx::Status::from_result(self.process_events(&suite, true).await);
        self.send_status(status, sender)?;
        self.process_events(&suite, false).await?;
        Ok(())
    }

    async fn process_events(
        &mut self,
        suite: &test_manager::SuiteControllerProxy,
        break_on_ready: bool,
    ) -> Result<(), zx::Status> {
        loop {
            if match self.state {
                SuiteState::Stopped => true,
                SuiteState::Ready(_) => break_on_ready,
                _ => false,
            } {
                break;
            }
            let events = match suite.get_events().await {
                Err(fidl::Error::ClientChannelClosed { status, .. }) => match status {
                    zx::Status::PEER_CLOSED => Ok(Vec::new()),
                    other => Err(other),
                },
                Err(e) => {
                    fx_log_err!("SuiteController.GetEvents failed: {:?}", e);
                    Err(zx::Status::INTERNAL)
                }
                Ok(result) => match result {
                    Err(e) => {
                        fx_log_warn!("SuiteController.GetEvents returned error: {:?}", e);
                        match e {
                            test_manager::LaunchError::ResourceUnavailable => {
                                Err(zx::Status::NO_RESOURCES)
                            }
                            test_manager::LaunchError::InstanceCannotResolve => {
                                Err(zx::Status::NOT_FOUND)
                            }
                            test_manager::LaunchError::InvalidArgs => Err(zx::Status::INVALID_ARGS),
                            test_manager::LaunchError::FailedToConnectToTestSuite => {
                                Err(zx::Status::NOT_SUPPORTED)
                            }
                            _ => Err(zx::Status::INTERNAL),
                        }
                    }
                    Ok(events) => Ok(events),
                },
            }?;
            if events.is_empty() {
                self.state = SuiteState::Stopped;
            }
            for event in events {
                if let Some(payload) = event.payload {
                    match payload {
                        test_manager::SuiteEventPayload::SuiteArtifact(suite_artifact) => {
                            self.handle_artifact(suite_artifact.artifact)?;
                        }
                        test_manager::SuiteEventPayload::CaseStarted(_) => {
                            if let SuiteState::Initial(flags) = self.state {
                                self.state = SuiteState::Started(flags);
                            } else {
                                fx_log_err!("Multiple cases started; not a fuzzer");
                                return Err(zx::Status::NOT_SUPPORTED);
                            }
                        }
                        test_manager::SuiteEventPayload::CaseArtifact(case_artifact) => {
                            self.handle_artifact(case_artifact.artifact)?;
                        }
                        test_manager::SuiteEventPayload::CaseStopped(_) => {
                            self.state = SuiteState::Stopped;
                            break;
                        }
                        test_manager::SuiteEventPayload::SuiteStopped(_) => {
                            self.state = SuiteState::Stopped;
                            break;
                        }
                        _ => {}
                    };
                }
            }
        }
        Ok(())
    }

    fn handle_artifact(&mut self, artifact: test_manager::Artifact) -> Result<(), zx::Status> {
        let mut flags = match self.state {
            SuiteState::Initial(flags) => flags,
            SuiteState::Started(flags) => flags,
            SuiteState::Ready(flags) => flags,
            SuiteState::Stopped => unreachable!(),
        };
        match artifact {
            test_manager::Artifact::Stdout(socket) => {
                flags.has_stdout = true;
                self.out_sink.unbounded_send(socket).map_err(|_| zx::Status::CANCELED)
            }
            test_manager::Artifact::Stderr(socket) => {
                flags.has_stderr = true;
                self.err_sink.unbounded_send(socket).map_err(|_| zx::Status::CANCELED)
            }
            test_manager::Artifact::Log(syslog) => match syslog {
                test_manager::Syslog::Archive(client) => {
                    flags.has_syslog = true;
                    self.log_sink.unbounded_send(client).map_err(|_| zx::Status::CANCELED)
                }
                _ => Err(zx::Status::NOT_SUPPORTED),
            },
            _ => Err(zx::Status::NOT_SUPPORTED),
        }?;
        match self.state {
            SuiteState::Initial(_) => {
                self.state = SuiteState::Initial(flags);
            }
            SuiteState::Started(_) => {
                if flags == DiagnosticFlags::all() {
                    self.state = SuiteState::Ready(flags);
                } else {
                    self.state = SuiteState::Started(flags);
                }
            }
            _ => {}
        }
        Ok(())
    }

    fn send_status(
        &self,
        mut status: zx::Status,
        sender: oneshot::Sender<zx::Status>,
    ) -> Result<(), zx::Status> {
        if status == zx::Status::OK && self.state == SuiteState::Stopped {
            status = zx::Status::BAD_STATE;
        }
        if let Err(_) = sender.send(status) {
            status = zx::Status::INTERNAL;
        }
        zx::Status::ok(status.into_raw())
    }
}
