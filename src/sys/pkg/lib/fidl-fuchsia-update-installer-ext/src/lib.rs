// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! `fidl_fuchsia_update_installer_ext` contains wrapper types around the auto-generated
//! `fidl_fuchsia_update_installer` bindings.

pub mod state;
pub use state::{Progress, State, UpdateInfo, UpdateInfoAndProgress};

pub mod options;
pub use options::{Initiator, Options};

use {
    fidl::endpoints::ServerEnd,
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
    std::{convert::TryInto, pin::Pin},
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
pub struct UpdateAttempt {
    ///  UUID identifying the requested update attempt.
    attempt_id: String,

    /// Client end of a MonitorRequest.
    #[pin]
    stream: MonitorRequestStream,
}

impl UpdateAttempt {
    /// Getter for the attempt_id.
    pub fn attempt_id(&self) -> &str {
        &self.attempt_id
    }
}

/// Checks if an update can be started and returns the UpdateAttempt containing
/// the attempt_id and MonitorRequestStream to the client.
pub async fn start_update(
    update_url: &PkgUrl,
    options: Options,
    installer_proxy: InstallerProxy,
    reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
) -> Result<UpdateAttempt, UpdateAttemptError> {
    let mut url = fidl_fuchsia_pkg::PackageUrl { url: update_url.to_string() };
    let (monitor_client_end, monitor) = fidl::endpoints::create_request_stream::<MonitorMarker>()
        .map_err(UpdateAttemptError::FIDL)?;
    let attempt_id = installer_proxy
        .start_update(&mut url, options.into(), monitor_client_end, reboot_controller_server_end)
        .await
        .map_err(UpdateAttemptError::FIDL)?
        .map_err(|reason| match reason {
            UpdateNotStartedReason::AlreadyInProgress => UpdateAttemptError::InstallInProgress,
        })?;
    info!("Update started with attempt id: {}", attempt_id);
    Ok(UpdateAttempt { attempt_id, stream: monitor })
}

impl Stream for UpdateAttempt {
    type Item = Result<State, MonitorUpdateAttemptError>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let UpdateAttemptProj { attempt_id: _, stream } = self.project();
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_update_installer::{InstallationProgress, InstallerMarker, InstallerRequest},
        fuchsia_async as fasync,
        futures::stream::StreamExt,
        matches::assert_matches,
    };

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";

    #[fasync::run_singlethreaded(test)]
    async fn test_start_update() {
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
                start_update(&pkgurl, opts, proxy, Some(reboot_controller_server_end))
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

        let expected_state = State::FailPrepare;
        let installer_fut = async move {
            let mut returned_update_attempt =
                start_update(&pkgurl, opts, proxy, Some(reboot_controller_server_end))
                    .await
                    .unwrap();
            while let Some(state) = returned_update_attempt.try_next().await.unwrap() {
                assert_eq!(expected_state, state);
            }
        };

        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000002".to_owned()))
                        .unwrap();

                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor.on_state(&mut State::FailPrepare.into()).await.unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_error() {
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
            match start_update(&pkgurl, opts, proxy, None).await {
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
                start_update(&pkgurl, opts, proxy, Some(reboot_controller_server_end))
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

                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor
                        .on_state(&mut fidl_fuchsia_update_installer::State::Fetch(
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
                        .await
                        .unwrap();
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

        let (_reboot_controller, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>().unwrap();

        let installer_fut = async move {
            let mut returned_update_attempt =
                start_update(&pkgurl, opts, proxy, Some(reboot_controller_server_end))
                    .await
                    .unwrap();
            let mut state_machine_step = 0;
            while let Some(state) = returned_update_attempt.try_next().await.unwrap() {
                assert_eq!(expected_states[state_machine_step], state);
                state_machine_step += 1;
            }
            assert_eq!(state_machine_step, expected_states.len());
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000003".to_owned()))
                        .unwrap();

                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor
                        .on_state(&mut fidl_fuchsia_update_installer::State::Prepare(
                            fidl_fuchsia_update_installer::PrepareData {},
                        ))
                        .await
                        .unwrap();
                    let () = monitor
                        .on_state(&mut fidl_fuchsia_update_installer::State::Fetch(
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
                        .await
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }
}
