// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! `fidl_fuchsia_update_installer_ext` contains wrapper types around the auto-generated
//! `fidl_fuchsia_update_installer` bindings.

pub mod state;
pub use state::{Progress, State, StateId, UpdateInfo, UpdateInfoAndProgress};

pub mod options;
pub use options::{Initiator, Options};

use {
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_update_installer::{
        InstallerProxy, MonitorMarker, MonitorRequest, MonitorRequestStream,
        RebootControllerMarker, UpdateNotStartedReason,
    },
    fuchsia_url::pkg_url::PkgUrl,
    futures::{
        prelude::*,
        task::{Context, Poll},
    },
    log::info,
    pin_project::pin_project,
    std::{convert::TryInto, fmt, pin::Pin},
    thiserror::Error,
};

/// Describes the errors encountered by UpdateAttempt.
#[derive(Debug, Error)]
pub enum UpdateAttemptError {
    /// Fidl error.
    #[error("FIDL error")]
    FIDL(#[source] fidl::Error),

    /// Install already in progress.
    #[error("an installation was already in progress")]
    InstallInProgress,
}

/// Describes the errors encountered by the UpdateAttempt's monitor stream.
#[derive(Debug, Error)]
pub enum MonitorUpdateAttemptError {
    /// Fidl error.
    #[error("FIDL error")]
    FIDL(#[source] fidl::Error),

    /// Error while decoding a [`fidl_fuchsia_update_installer::State`].
    #[error("unable to decode State")]
    DecodeState(#[source] state::DecodeStateError),
}

/// An update attempt.
#[pin_project(project = UpdateAttemptProj)]
#[derive(Debug)]
pub struct UpdateAttempt {
    /// UUID identifying the update attempt.
    attempt_id: String,

    /// The monitor for this update attempt.
    #[pin]
    monitor: UpdateAttemptMonitor,
}

/// A monitor of an update attempt.
#[pin_project(project = UpdateAttemptMonitorProj)]
pub struct UpdateAttemptMonitor {
    /// Server end of a fidl_fuchsia_update_installer.Monitor protocol.
    #[pin]
    stream: MonitorRequestStream,
}

impl UpdateAttempt {
    /// Getter for the attempt_id.
    pub fn attempt_id(&self) -> &str {
        &self.attempt_id
    }
}

impl UpdateAttemptMonitor {
    fn new() -> Result<(ClientEnd<MonitorMarker>, Self), fidl::Error> {
        let (monitor_client_end, stream) =
            fidl::endpoints::create_request_stream::<MonitorMarker>()?;

        Ok((monitor_client_end, Self { stream }))
    }
}

/// Checks if an update can be started and returns the UpdateAttempt containing
/// the attempt_id and MonitorRequestStream to the client.
pub async fn start_update(
    update_url: &PkgUrl,
    options: Options,
    installer_proxy: &InstallerProxy,
    reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
) -> Result<UpdateAttempt, UpdateAttemptError> {
    let mut url = fidl_fuchsia_pkg::PackageUrl { url: update_url.to_string() };
    let (monitor_client_end, monitor) =
        UpdateAttemptMonitor::new().map_err(UpdateAttemptError::FIDL)?;

    let attempt_id = installer_proxy
        .start_update(&mut url, options.into(), monitor_client_end, reboot_controller_server_end)
        .await
        .map_err(UpdateAttemptError::FIDL)?
        .map_err(|reason| match reason {
            UpdateNotStartedReason::AlreadyInProgress => UpdateAttemptError::InstallInProgress,
        })?;

    info!("Update started with attempt id: {}", attempt_id);
    Ok(UpdateAttempt { attempt_id, monitor })
}

/// Monitors the running update attempt given by `attempt_id`, or any running update attempt if no
/// `attempt_id` is provided.
pub async fn monitor_update(
    attempt_id: Option<&str>,
    installer_proxy: &InstallerProxy,
) -> Result<Option<UpdateAttemptMonitor>, fidl::Error> {
    let (monitor_client_end, monitor) = UpdateAttemptMonitor::new()?;

    let attached = installer_proxy.monitor_update(attempt_id, monitor_client_end).await?;

    if attached {
        Ok(Some(monitor))
    } else {
        Ok(None)
    }
}

impl fmt::Debug for UpdateAttemptMonitor {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("UpdateAttemptMonitor").field("stream", &"MonitorRequestStream").finish()
    }
}

impl Stream for UpdateAttemptMonitor {
    type Item = Result<State, MonitorUpdateAttemptError>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let UpdateAttemptMonitorProj { stream } = self.project();
        let poll_res = match stream.poll_next(cx) {
            Poll::Ready(None) => return Poll::Ready(None),
            Poll::Ready(Some(res)) => res.map_err(MonitorUpdateAttemptError::FIDL)?,
            Poll::Pending => return Poll::Pending,
        };
        let MonitorRequest::OnState { state, responder } = poll_res;
        let _ = responder.send();
        let state = state.try_into().map_err(MonitorUpdateAttemptError::DecodeState)?;
        Poll::Ready(Some(Ok(state)))
    }
}

impl Stream for UpdateAttempt {
    type Item = Result<State, MonitorUpdateAttemptError>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let UpdateAttemptProj { attempt_id: _, monitor } = self.project();
        monitor.poll_next(cx)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_update_installer::{
            InstallationProgress, InstallerMarker, InstallerRequest, MonitorProxy,
        },
        fuchsia_async as fasync,
        futures::stream::StreamExt,
        matches::assert_matches,
    };

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";

    impl UpdateAttemptMonitor {
        /// Returns an UpdateAttemptMonitor and a TestAttempt that can be used to send states to
        /// the monitor.
        fn new_test() -> (TestAttempt, Self) {
            let (monitor_client_end, monitor) = Self::new().unwrap();

            (TestAttempt::new(monitor_client_end), monitor)
        }
    }

    struct TestAttempt {
        proxy: MonitorProxy,
    }

    impl TestAttempt {
        /// Wraps the given monitor proxy in a helper type that verifies sending state to the
        /// remote end of the Monitor results in state being acknowledged as expected.
        fn new(monitor_client_end: ClientEnd<MonitorMarker>) -> Self {
            let proxy = monitor_client_end.into_proxy().unwrap();

            Self { proxy }
        }

        async fn send_state_and_recv_ack(&mut self, state: State) {
            self.send_raw_state_and_recv_ack(state.into()).await;
        }

        async fn send_raw_state_and_recv_ack(
            &mut self,
            mut state: fidl_fuchsia_update_installer::State,
        ) {
            let () = self.proxy.on_state(&mut state).await.unwrap();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn update_attempt_monitor_forwards_and_acks_progress() {
        let (mut send, monitor) = UpdateAttemptMonitor::new_test();

        let expected_fetch_state = &State::Fetch(
            UpdateInfoAndProgress::builder()
                .info(UpdateInfo::builder().download_size(1000).build())
                .progress(Progress::builder().fraction_completed(0.5).bytes_downloaded(500).build())
                .build(),
        );

        let client_fut = async move {
            assert_eq!(
                monitor.try_collect::<Vec<State>>().await.unwrap(),
                vec![State::Prepare, expected_fetch_state.clone()]
            );
        };

        let server_fut = async move {
            send.send_state_and_recv_ack(State::Prepare).await;
            send.send_state_and_recv_ack(expected_fetch_state.clone()).await;
        };

        future::join(client_fut, server_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn update_attempt_monitor_rejects_invalid_state() {
        let (mut send, mut monitor) = UpdateAttemptMonitor::new_test();

        let client_fut = async move {
            assert_matches!(
                monitor.next().await.unwrap(),
                Err(MonitorUpdateAttemptError::DecodeState(_))
            );
            assert_matches!(monitor.next().await, Some(Ok(State::Prepare)));
        };

        let server_fut = async move {
            send.send_raw_state_and_recv_ack(fidl_fuchsia_update_installer::State::Fetch(
                fidl_fuchsia_update_installer::FetchData {
                    info: Some(fidl_fuchsia_update_installer::UpdateInfo { download_size: None }),
                    progress: Some(InstallationProgress {
                        fraction_completed: Some(2.0),
                        bytes_downloaded: None,
                    }),
                },
            ))
            .await;

            // Even though the previous state was invalid and the monitor stream yielded an error,
            // further states will continue to be processed by the client.
            send.send_state_and_recv_ack(State::Prepare).await;
        };

        future::join(client_fut, server_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_update_forwards_args_and_returns_attempt_id() {
        let pkgurl =
            PkgUrl::new_package("fuchsia.com".to_string(), "/update/0".to_string(), None).unwrap();

        let opts = Options {
            initiator: Initiator::User,
            allow_attach_to_existing_attempt: false,
            should_write_recovery: true,
        };

        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let (_reboot_controller, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>().unwrap();

        let installer_fut = async move {
            let returned_update_attempt =
                start_update(&pkgurl, opts, &proxy, Some(reboot_controller_server_end))
                    .await
                    .unwrap();
            assert_eq!(
                returned_update_attempt.attempt_id(),
                "00000000-0000-0000-0000-000000000001"
            );
        };

        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate {
                    url,
                    options:
                        fidl_fuchsia_update_installer::Options {
                            initiator,
                            should_write_recovery,
                            allow_attach_to_existing_attempt,
                        },
                    monitor: _,
                    reboot_controller,
                    responder,
                }) => {
                    assert_eq!(url.url, TEST_URL);
                    assert_eq!(initiator, Some(fidl_fuchsia_update_installer::Initiator::User));
                    assert_matches!(reboot_controller, Some(_));
                    assert_eq!(should_write_recovery, Some(true));
                    assert_eq!(allow_attach_to_existing_attempt, Some(false));
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000001".to_owned()))
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_install_error() {
        let pkgurl =
            PkgUrl::new_package("fuchsia.com".to_string(), "/update/0".to_string(), None).unwrap();

        let opts = Options {
            initiator: Initiator::User,
            allow_attach_to_existing_attempt: false,
            should_write_recovery: true,
        };

        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let (_reboot_controller, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>().unwrap();

        let installer_fut = async move {
            let returned_update_attempt =
                start_update(&pkgurl, opts, &proxy, Some(reboot_controller_server_end))
                    .await
                    .unwrap();

            assert_eq!(
                returned_update_attempt.try_collect::<Vec<State>>().await.unwrap(),
                vec![State::FailPrepare]
            );
        };

        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000002".to_owned()))
                        .unwrap();

                    let mut attempt = TestAttempt::new(monitor);
                    attempt.send_state_and_recv_ack(State::FailPrepare).await;
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn start_update_forwards_fidl_error() {
        let pkgurl =
            PkgUrl::new_package("fuchsia.com".to_string(), "/update/0".to_string(), None).unwrap();

        let opts = Options {
            initiator: Initiator::User,
            allow_attach_to_existing_attempt: false,
            should_write_recovery: true,
        };

        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let installer_fut = async move {
            match start_update(&pkgurl, opts, &proxy, None).await {
                Err(UpdateAttemptError::FIDL(_)) => {} // expected
                _ => panic!("Unexpected result"),
            }
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { .. }) => {
                    // Don't send attempt id.
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_state_decode_error() {
        let pkgurl =
            PkgUrl::new_package("fuchsia.com".to_string(), "/update/0".to_string(), None).unwrap();

        let opts = Options {
            initiator: Initiator::User,
            allow_attach_to_existing_attempt: false,
            should_write_recovery: true,
        };

        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let (_reboot_controller, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>().unwrap();

        let installer_fut = async move {
            let mut returned_update_attempt =
                start_update(&pkgurl, opts, &proxy, Some(reboot_controller_server_end))
                    .await
                    .unwrap();
            assert_matches!(
                returned_update_attempt.next().await,
                Some(Err(MonitorUpdateAttemptError::DecodeState(
                    state::DecodeStateError::DecodeProgress(
                        state::DecodeProgressError::FractionCompletedOutOfRange
                    )
                )))
            );
        };

        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000002".to_owned()))
                        .unwrap();

                    let mut monitor = TestAttempt::new(monitor);
                    monitor
                        .send_raw_state_and_recv_ack(fidl_fuchsia_update_installer::State::Fetch(
                            fidl_fuchsia_update_installer::FetchData {
                                info: Some(fidl_fuchsia_update_installer::UpdateInfo {
                                    download_size: None,
                                }),
                                progress: Some(InstallationProgress {
                                    fraction_completed: Some(2.0),
                                    bytes_downloaded: None,
                                }),
                            },
                        ))
                        .await;
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_server_close_unexpectedly() {
        let pkgurl =
            PkgUrl::new_package("fuchsia.com".to_string(), "/update/0".to_string(), None).unwrap();

        let opts = Options {
            initiator: Initiator::User,
            allow_attach_to_existing_attempt: false,
            should_write_recovery: true,
        };

        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let (_reboot_controller, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>().unwrap();

        let expected_states = vec![
            State::Prepare,
            State::Fetch(
                UpdateInfoAndProgress::builder()
                    .info(UpdateInfo::builder().download_size(0).build())
                    .progress(
                        Progress::builder().fraction_completed(0.0).bytes_downloaded(0).build(),
                    )
                    .build(),
            ),
        ];

        let installer_fut = async move {
            let returned_update_attempt =
                start_update(&pkgurl, opts, &proxy, Some(reboot_controller_server_end))
                    .await
                    .unwrap();

            assert_eq!(
                returned_update_attempt.try_collect::<Vec<State>>().await.unwrap(),
                expected_states,
            );
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000003".to_owned()))
                        .unwrap();

                    let mut monitor = TestAttempt::new(monitor);
                    monitor.send_state_and_recv_ack(State::Prepare).await;
                    monitor
                        .send_raw_state_and_recv_ack(fidl_fuchsia_update_installer::State::Fetch(
                            fidl_fuchsia_update_installer::FetchData {
                                info: Some(fidl_fuchsia_update_installer::UpdateInfo {
                                    download_size: None,
                                }),
                                progress: Some(InstallationProgress {
                                    fraction_completed: Some(0.0),
                                    bytes_downloaded: None,
                                }),
                            },
                        ))
                        .await;

                    // monitor never sends a terminal state, but the client stream doesn't mind.
                    // Higher layers of the system (ex. omaha-client/system-update-checker) convert
                    // this situation into an error.
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn monitor_update_uses_provided_attempt_id() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let client_fut = async move {
            let _ = monitor_update(Some("id"), &proxy).await;
        };

        let server_fut = async move {
            match stream.next().await.unwrap().unwrap() {
                InstallerRequest::MonitorUpdate { attempt_id, .. } => {
                    assert_eq!(attempt_id.as_ref().map(String::as_str), Some("id"));
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };

        future::join(client_fut, server_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn monitor_update_handles_no_update_in_progress() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let client_fut = async move {
            assert_matches!(monitor_update(None, &proxy).await, Ok(None));
        };

        let server_fut = async move {
            match stream.next().await.unwrap().unwrap() {
                InstallerRequest::MonitorUpdate { attempt_id, monitor, responder } => {
                    assert_eq!(attempt_id, None);
                    drop(monitor);
                    responder.send(false).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
            assert_matches!(stream.next().await, None);
        };

        future::join(client_fut, server_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn monitor_update_forwards_fidl_error() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let client_fut = async move {
            assert_matches!(monitor_update(None, &proxy).await, Err(_));
        };
        let server_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::MonitorUpdate { .. }) => {
                    // Close the channel instead of sending a response.
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(client_fut, server_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn monitor_update_forwards_and_acks_progress() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();

        let client_fut = async move {
            let monitor = monitor_update(None, &proxy).await.unwrap().unwrap();

            assert_eq!(
                monitor.try_collect::<Vec<State>>().await.unwrap(),
                vec![State::Prepare, State::FailPrepare]
            );
        };

        let server_fut = async move {
            match stream.next().await.unwrap().unwrap() {
                InstallerRequest::MonitorUpdate { attempt_id, monitor, responder } => {
                    assert_eq!(attempt_id, None);
                    responder.send(true).unwrap();
                    let mut monitor = TestAttempt::new(monitor);

                    monitor.send_state_and_recv_ack(State::Prepare).await;
                    monitor.send_state_and_recv_ack(State::FailPrepare).await;
                }
                request => panic!("Unexpected request: {:?}", request),
            }
            assert_matches!(stream.next().await, None);
        };

        future::join(client_fut, server_fut).await;
    }
}
