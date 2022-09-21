// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{
        CaseStatus, DebugDataIteratorMarker, RunEvent as FidlRunEvent,
        RunEventPayload as FidlRunEventPayload, SuiteEvent as FidlSuiteEvent,
        SuiteEventPayload as FidlSuiteEventPayload, SuiteStatus,
    },
    fuchsia_zircon as zx,
};

pub(crate) enum RunEventPayload {
    DebugData(ClientEnd<DebugDataIteratorMarker>),
}

pub(crate) struct RunEvent {
    timestamp: i64,
    payload: RunEventPayload,
}

impl Into<FidlRunEvent> for RunEvent {
    fn into(self) -> FidlRunEvent {
        match self.payload {
            RunEventPayload::DebugData(client) => FidlRunEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlRunEventPayload::Artifact(ftest_manager::Artifact::DebugData(
                    client,
                ))),
                ..FidlRunEvent::EMPTY
            },
        }
    }
}

impl RunEvent {
    pub fn debug_data(client: ClientEnd<DebugDataIteratorMarker>) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: RunEventPayload::DebugData(client),
        }
    }

    #[cfg(test)]
    pub fn into_payload(self) -> RunEventPayload {
        self.payload
    }
}

enum SuiteEventPayload {
    CaseFound(String, u32),
    CaseStarted(u32),
    CaseStopped(u32, CaseStatus),
    CaseFinished(u32),
    CaseStdout(u32, zx::Socket),
    CaseStderr(u32, zx::Socket),
    CustomArtifact(ftest_manager::CustomArtifact),
    SuiteSyslog(ftest_manager::Syslog),
    SuiteStarted,
    SuiteStopped(SuiteStatus),
}

pub struct SuiteEvents {
    timestamp: i64,
    payload: SuiteEventPayload,
}

impl Into<FidlSuiteEvent> for SuiteEvents {
    fn into(self) -> FidlSuiteEvent {
        match self.payload {
            SuiteEventPayload::CaseFound(name, identifier) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::CaseFound(ftest_manager::CaseFound {
                    test_case_name: name,
                    identifier,
                })),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::CaseStarted(identifier) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::CaseStarted(ftest_manager::CaseStarted {
                    identifier,
                })),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::CaseStopped(identifier, status) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::CaseStopped(ftest_manager::CaseStopped {
                    identifier,
                    status,
                })),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::CaseFinished(identifier) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::CaseFinished(ftest_manager::CaseFinished {
                    identifier,
                })),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::CaseStdout(identifier, socket) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::CaseArtifact(ftest_manager::CaseArtifact {
                    identifier,
                    artifact: ftest_manager::Artifact::Stdout(socket),
                })),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::CaseStderr(identifier, socket) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::CaseArtifact(ftest_manager::CaseArtifact {
                    identifier,
                    artifact: ftest_manager::Artifact::Stderr(socket),
                })),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::CustomArtifact(custom) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::SuiteArtifact(ftest_manager::SuiteArtifact {
                    artifact: ftest_manager::Artifact::Custom(custom),
                })),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::SuiteSyslog(syslog) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::SuiteArtifact(ftest_manager::SuiteArtifact {
                    artifact: ftest_manager::Artifact::Log(syslog),
                })),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::SuiteStarted => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::SuiteStarted(ftest_manager::SuiteStarted {})),
                ..FidlSuiteEvent::EMPTY
            },
            SuiteEventPayload::SuiteStopped(status) => FidlSuiteEvent {
                timestamp: Some(self.timestamp),
                payload: Some(FidlSuiteEventPayload::SuiteStopped(ftest_manager::SuiteStopped {
                    status,
                })),
                ..FidlSuiteEvent::EMPTY
            },
        }
    }
}

impl SuiteEvents {
    pub fn case_found(identifier: u32, name: String) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::CaseFound(name, identifier),
        }
    }

    pub fn case_started(identifier: u32) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::CaseStarted(identifier),
        }
    }

    pub fn case_stopped(identifier: u32, status: CaseStatus) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::CaseStopped(identifier, status),
        }
    }

    pub fn case_finished(identifier: u32) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::CaseFinished(identifier),
        }
    }

    pub fn case_stdout(identifier: u32, socket: zx::Socket) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::CaseStdout(identifier, socket),
        }
    }

    pub fn case_stderr(identifier: u32, socket: zx::Socket) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::CaseStderr(identifier, socket),
        }
    }

    pub fn suite_syslog(syslog: ftest_manager::Syslog) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::SuiteSyslog(syslog),
        }
    }

    pub fn suite_custom_artifact(custom: ftest_manager::CustomArtifact) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::CustomArtifact(custom),
        }
    }

    pub fn suite_started() -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::SuiteStarted,
        }
    }

    pub fn suite_stopped(status: SuiteStatus) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic().into_nanos(),
            payload: SuiteEventPayload::SuiteStopped(status),
        }
    }

    #[cfg(test)]
    pub fn into_suite_run_event(self) -> FidlSuiteEvent {
        self.into()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[test]
    fn suite_events() {
        let event = SuiteEvents::case_found(1, "case1".to_string()).into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_eq!(
            event.payload,
            Some(FidlSuiteEventPayload::CaseFound(ftest_manager::CaseFound {
                test_case_name: "case1".into(),
                identifier: 1
            }))
        );

        let event = SuiteEvents::case_started(2).into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_eq!(
            event.payload,
            Some(FidlSuiteEventPayload::CaseStarted(ftest_manager::CaseStarted { identifier: 2 }))
        );

        let event = SuiteEvents::case_stopped(2, CaseStatus::Failed).into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_eq!(
            event.payload,
            Some(FidlSuiteEventPayload::CaseStopped(ftest_manager::CaseStopped {
                identifier: 2,
                status: CaseStatus::Failed
            }))
        );

        let event = SuiteEvents::case_finished(2).into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_eq!(
            event.payload,
            Some(FidlSuiteEventPayload::CaseFinished(ftest_manager::CaseFinished {
                identifier: 2
            }))
        );

        let (sock1, _sock2) = zx::Socket::create(zx::SocketOpts::empty()).unwrap();
        let event = SuiteEvents::case_stdout(2, sock1).into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_matches!(
            event.payload,
            Some(FidlSuiteEventPayload::CaseArtifact(ftest_manager::CaseArtifact {
                identifier: 2,
                artifact: ftest_manager::Artifact::Stdout(_)
            }))
        );

        let (sock1, _sock2) = zx::Socket::create(zx::SocketOpts::empty()).unwrap();
        let event = SuiteEvents::case_stderr(2, sock1).into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_matches!(
            event.payload,
            Some(FidlSuiteEventPayload::CaseArtifact(ftest_manager::CaseArtifact {
                identifier: 2,
                artifact: ftest_manager::Artifact::Stderr(_)
            }))
        );

        let event = SuiteEvents::suite_stopped(SuiteStatus::Failed).into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_eq!(
            event.payload,
            Some(FidlSuiteEventPayload::SuiteStopped(ftest_manager::SuiteStopped {
                status: SuiteStatus::Failed,
            }))
        );

        let (client_end, _server_end) = fidl::endpoints::create_endpoints().unwrap();
        let event = SuiteEvents::suite_syslog(ftest_manager::Syslog::Archive(client_end))
            .into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_matches!(
            event.payload,
            Some(FidlSuiteEventPayload::SuiteArtifact(ftest_manager::SuiteArtifact {
                artifact: ftest_manager::Artifact::Log(ftest_manager::Syslog::Archive(_)),
            }))
        );

        let (client_end, _server_end) = fidl::endpoints::create_endpoints().unwrap();
        let event = SuiteEvents::suite_syslog(ftest_manager::Syslog::Batch(client_end))
            .into_suite_run_event();
        assert_matches!(event.timestamp, Some(_));
        assert_matches!(
            event.payload,
            Some(FidlSuiteEventPayload::SuiteArtifact(ftest_manager::SuiteArtifact {
                artifact: ftest_manager::Artifact::Log(ftest_manager::Syslog::Batch(_)),
            }))
        );
    }
}
