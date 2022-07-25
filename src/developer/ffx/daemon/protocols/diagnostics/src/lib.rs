// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of the daemon diagnostics streaming protocol.

use {
    anyhow::bail,
    anyhow::{Context as _, Result},
    async_trait::async_trait,
    diagnostics::{get_streaming_min_timestamp, run_diagnostics_streaming},
    ffx_daemon_target::logger::streamer::{DiagnosticsStreamer, GenericDiagnosticsStreamer},
    fidl_fuchsia_developer_ffx as ffx,
    fuchsia_async::TimeoutExt,
    futures::FutureExt,
    protocols::prelude::*,
    std::sync::Arc,
    std::time::Duration,
};

#[ffx_protocol(ffx::TargetCollectionMarker)]
#[derive(Default)]
pub struct Diagnostics {}

#[async_trait(?Send)]
impl FidlProtocol for Diagnostics {
    type Protocol = ffx::DiagnosticsMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: ffx::DiagnosticsRequest) -> Result<()> {
        match req {
            ffx::DiagnosticsRequest::StreamDiagnostics {
                target: target_str,
                parameters,
                iterator,
                responder,
            } => {
                let stream_mode = if let Some(mode) = parameters.stream_mode {
                    mode
                } else {
                    tracing::info!("StreamDiagnostics failed: stream mode is required");
                    return responder
                        .send(&mut Err(ffx::DiagnosticsStreamError::MissingParameter))
                        .context("sending missing parameter response");
                };

                if stream_mode == ffx::StreamMode::SnapshotAll {
                    let target_str = if let Some(target_str) = target_str {
                        target_str
                    } else {
                        tracing::warn!(
                            "StreamDiagnostics failed: Missing target string in SnapshotAll mode."
                        );
                        return responder
                            .send(&mut Err(ffx::DiagnosticsStreamError::MissingParameter))
                            .context("sending missing parameter response");
                    };

                    let session = if let Some(ref session) = parameters.session {
                        session
                    } else {
                        tracing::warn!(
                            "StreamDiagnostics failed: Missing session in SnapshotAll mode."
                        );
                        return responder
                            .send(&mut Err(ffx::DiagnosticsStreamError::MissingParameter))
                            .context("sending missing parameter response");
                    };

                    let mut streams =
                        DiagnosticsStreamer::list_sessions(Some(target_str.clone())).await?;
                    if streams.is_empty() {
                        responder.send(&mut Err(
                            ffx::DiagnosticsStreamError::NoMatchingOfflineTargets,
                        ))?;
                        return Ok(());
                    }

                    let streams = streams
                        .remove(&target_str)
                        .context("getting stream by target name. should be infallible")?;

                    if streams.is_empty() {
                        responder.send(&mut Err(
                            ffx::DiagnosticsStreamError::NoMatchingOfflineTargets,
                        ))?;
                        return Ok(());
                    }

                    let (target_str, stream) = match session {
                        ffx::SessionSpec::TimestampNanos(ref ts) => {
                            let mut result_stream = None;

                            for stream in streams.into_iter() {
                                let session_ts = stream.session_timestamp_nanos().await;
                                if Some(ts) == session_ts.map(|t| t as u64).as_ref() {
                                    result_stream = Some(Arc::new(stream));
                                    break;
                                }
                            }

                            if let Some(stream) = result_stream {
                                (target_str, stream)
                            } else {
                                responder.send(&mut Err(
                                    ffx::DiagnosticsStreamError::NoMatchingOfflineSessions,
                                ))?;
                                return Ok(());
                            }
                        }
                        ffx::SessionSpec::Relative(rel) => {
                            let mut sorted = vec![];
                            for stream in streams.into_iter() {
                                let ts = stream.session_timestamp_nanos().await;
                                if let Some(ts) = ts {
                                    sorted.push((ts, stream));
                                }
                            }

                            sorted.sort_by_key(|t| -t.0);

                            if let Some((_, stream)) = sorted.into_iter().nth(*rel as usize) {
                                (target_str, Arc::new(stream))
                            } else {
                                return responder
                                    .send(&mut Err(
                                        ffx::DiagnosticsStreamError::NoMatchingOfflineSessions,
                                    ))
                                    .context("sending no offline sessions response");
                            }
                        }
                        v => bail!("unexpected SessionSpec value: {:?}", v),
                    };

                    match stream
                        .wait_for_setup()
                        .map(|_| Ok(()))
                        .on_timeout(Duration::from_secs(3), || {
                            Err(ffx::DiagnosticsStreamError::NoStreamForTarget)
                        })
                        .await
                    {
                        Ok(_) => {}
                        Err(e) => {
                            // TODO(jwing): we should be able to interact with inactive targets here for
                            // stream modes that don't involve subscription.
                            return responder.send(&mut Err(e)).context("sending error response");
                        }
                    }
                    let min_timestamp =
                        match get_streaming_min_timestamp(&parameters, &stream).await {
                            Ok(n) => n,
                            Err(e) => {
                                responder.send(&mut Err(e))?;
                                return Ok(());
                            }
                        };
                    let log_iterator = stream
                        .stream_entries(parameters.stream_mode.unwrap(), min_timestamp)
                        .await?;
                    responder.send(&mut Ok(ffx::LogSession {
                        target_identifier: Some(target_str),
                        session_timestamp_nanos: stream
                            .session_timestamp_nanos()
                            .await
                            .map(|t| t as u64),
                        ..ffx::LogSession::EMPTY
                    }))?;
                    run_diagnostics_streaming(log_iterator, iterator).await.map_err(|e| {
                        tracing::error!("Failure running diagnostics streaming: {:?}", e);
                        e
                    })?;
                    Ok(())
                } else {
                    // TODO(fxb/90340): Add a protocols utility library for feed-forward things like this.
                    let tc_proxy = self.open_target_collection_proxy(cx).await?;
                    // cx.open_proxy::<ffx::TargetCollectionMarker>().await?;
                    let (target_handle, th_server_end) =
                        fidl::endpoints::create_proxy::<ffx::TargetMarker>()?;
                    // TODO(awdavies): Document that there needs to be a timeout handler on the client side, or just handle
                    // a timeout in here.
                    match tc_proxy
                        .open_target(
                            ffx::TargetQuery {
                                string_matcher: target_str.clone(),
                                ..ffx::TargetQuery::EMPTY
                            },
                            th_server_end,
                        )
                        .await?
                    {
                        Err(e) => {
                            tracing::error!(
                                "diagnostics encountered error while opening target {}: {:?}",
                                target_str.as_deref().unwrap_or("<unknown>"),
                                e
                            );
                            responder
                                .send(&mut Err(ffx::DiagnosticsStreamError::NoMatchingTargets))
                                .map_err(Into::into)
                        }
                        Ok(()) => responder
                            .send(
                                &mut target_handle
                                    .stream_active_diagnostics(parameters, iterator)
                                    .await?,
                            )
                            .map_err(Into::into),
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_developer_remotecontrol::ArchiveIteratorMarker;
    use protocols::testing::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_stream_mode() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Diagnostics>().build();
        let diagnostics_proxy = daemon.open_proxy::<ffx::DiagnosticsMarker>().await;
        let (_proxy, server) = fidl::endpoints::create_proxy::<ArchiveIteratorMarker>().unwrap();
        assert_eq!(
            diagnostics_proxy
                .stream_diagnostics(
                    Some("narbacular"),
                    ffx::DaemonDiagnosticsStreamParameters::EMPTY,
                    server
                )
                .await
                .unwrap(),
            Err(ffx::DiagnosticsStreamError::MissingParameter)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_target() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Diagnostics>().build();
        let diagnostics_proxy = daemon.open_proxy::<ffx::DiagnosticsMarker>().await;
        let (_proxy, server) = fidl::endpoints::create_proxy::<ArchiveIteratorMarker>().unwrap();
        assert_eq!(
            diagnostics_proxy
                .stream_diagnostics(
                    None,
                    ffx::DaemonDiagnosticsStreamParameters {
                        stream_mode: Some(ffx::StreamMode::SnapshotAll),
                        ..ffx::DaemonDiagnosticsStreamParameters::EMPTY
                    },
                    server
                )
                .await
                .unwrap(),
            Err(ffx::DiagnosticsStreamError::MissingParameter)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_session() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Diagnostics>().build();
        let diagnostics_proxy = daemon.open_proxy::<ffx::DiagnosticsMarker>().await;
        let (_proxy, server) = fidl::endpoints::create_proxy::<ArchiveIteratorMarker>().unwrap();
        assert_eq!(
            diagnostics_proxy
                .stream_diagnostics(
                    Some("flippity-flap"),
                    ffx::DaemonDiagnosticsStreamParameters {
                        stream_mode: Some(ffx::StreamMode::SnapshotAll),
                        ..ffx::DaemonDiagnosticsStreamParameters::EMPTY
                    },
                    server
                )
                .await
                .unwrap(),
            Err(ffx::DiagnosticsStreamError::MissingParameter)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_targets() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<Diagnostics>()
            .register_instanced_protocol_closure::<ffx::TargetCollectionMarker, _>(|_cx, req| {
                match req {
                    ffx::TargetCollectionRequest::OpenTarget { responder, .. } => responder
                        .send(&mut Err(ffx::OpenTargetError::TargetNotFound))
                        .map_err(Into::into),
                    _ => panic!("unsupported for this test"),
                }
            })
            .build();
        let diagnostics_proxy = daemon.open_proxy::<ffx::DiagnosticsMarker>().await;
        let (_proxy, server) = fidl::endpoints::create_proxy::<ArchiveIteratorMarker>().unwrap();
        assert_eq!(
            diagnostics_proxy
                .stream_diagnostics(
                    Some("flippity-flap"),
                    ffx::DaemonDiagnosticsStreamParameters {
                        stream_mode: Some(ffx::StreamMode::Subscribe),
                        ..ffx::DaemonDiagnosticsStreamParameters::EMPTY
                    },
                    server
                )
                .await
                .unwrap(),
            Err(ffx::DiagnosticsStreamError::NoMatchingTargets)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_subscribe_returns_empty_log_session() {
        let daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<Diagnostics>()
            .register_instanced_protocol_closure::<ffx::TargetCollectionMarker, _>(|_cx, req| {
                match req {
                    ffx::TargetCollectionRequest::OpenTarget { responder, target_handle, .. } => {
                        use futures::TryStreamExt;
                        let mut stream = target_handle.into_stream()?;
                        // Target handle task.
                        fuchsia_async::Task::local(async move {
                            while let Ok(Some(req)) = stream.try_next().await {
                                match req {
                                    ffx::TargetRequest::StreamActiveDiagnostics {
                                        responder,
                                        ..
                                    } => responder.send(&mut Ok(ffx::LogSession::EMPTY)).unwrap(),
                                    e => panic!("unsupported request for this target handle related test: {:?}", e),
                                }
                            }
                        })
                        .detach();
                        responder.send(&mut Ok(())).map_err(Into::into)
                    }
                    _ => panic!("unsupported for this test"),
                }
            })
            .build();
        let diagnostics_proxy = daemon.open_proxy::<ffx::DiagnosticsMarker>().await;
        let (_proxy, server) = fidl::endpoints::create_proxy::<ArchiveIteratorMarker>().unwrap();
        assert_eq!(
            diagnostics_proxy
                .stream_diagnostics(
                    Some("flippity-flap"),
                    ffx::DaemonDiagnosticsStreamParameters {
                        stream_mode: Some(ffx::StreamMode::Subscribe),
                        ..ffx::DaemonDiagnosticsStreamParameters::EMPTY
                    },
                    server
                )
                .await
                .unwrap(),
            Ok(ffx::LogSession::EMPTY)
        );
    }
}
