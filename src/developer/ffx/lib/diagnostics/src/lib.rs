// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library for Fuchsia device diagnostics utilities.

use {
    anyhow::Result,
    diagnostics_data::Timestamp,
    ffx_daemon_target::logger::streamer::{DiagnosticsStreamer, SessionStream},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_bridge::{
        DaemonDiagnosticsStreamParameters, DiagnosticsStreamError, TimeBound,
    },
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorRequest,
        DiagnosticsData, InlineData,
    },
    fuchsia_async::futures::{stream::TryStreamExt, AsyncWriteExt},
    std::sync::Arc,
};

pub async fn run_diagnostics_streaming(
    mut log_iterator: SessionStream,
    iterator: ServerEnd<ArchiveIteratorMarker>,
) -> Result<()> {
    let mut iter_stream = iterator.into_stream()?;
    while let Some(request) = iter_stream.try_next().await? {
        match request {
            ArchiveIteratorRequest::GetNext { responder } => {
                let res = log_iterator.iter().await?;
                match res {
                    Some(Ok(entry)) => {
                        // If the entry is small enough to fit into a FIDL message
                        // we send it using the Inline variant. Otherwise, we use
                        // the Socket variant by sending one end of the socket as a
                        // response and sending the data into the other end of the
                        // socket.
                        // TODO(fxbug.dev/81310): This should be unified across the
                        // daemon and bridge.
                        let data = serde_json::to_string(&entry)?;
                        if data.len() <= fidl_fuchsia_logger::MAX_DATAGRAM_LEN_BYTES as usize {
                            responder.send(&mut Ok(vec![ArchiveIteratorEntry {
                                diagnostics_data: Some(DiagnosticsData::Inline(InlineData {
                                    data,
                                    truncated_chars: 0,
                                })),
                                ..ArchiveIteratorEntry::EMPTY
                            }]))?;
                        } else {
                            let (socket, tx_socket) =
                                fuchsia_async::emulated_handle::Socket::create(
                                    fuchsia_async::emulated_handle::SocketOpts::STREAM,
                                )?;
                            let mut tx_socket = fuchsia_async::Socket::from_socket(tx_socket)?;
                            // We send one end of the socket back to the caller.
                            // The receiver will need to read the socket content to
                            // get the data.
                            let response = vec![ArchiveIteratorEntry {
                                diagnostics_data: Some(DiagnosticsData::Socket(socket)),
                                ..ArchiveIteratorEntry::EMPTY
                            }];
                            responder.send(&mut Ok(response))?;
                            // We write all the data to the other end of the
                            // socket.
                            tx_socket.write_all(data.as_bytes()).await?;
                        }
                    }
                    Some(Err(e)) => {
                        log::warn!("got error streaming diagnostics: {}", e);
                        responder.send(&mut Err(ArchiveIteratorError::DataReadFailed))?;
                    }
                    None => {
                        responder.send(&mut Ok(vec![]))?;
                        break;
                    }
                }
            }
        }
    }

    Ok::<(), anyhow::Error>(())
}

pub async fn get_streaming_min_timestamp(
    parameters: &DaemonDiagnosticsStreamParameters,
    stream: &Arc<DiagnosticsStreamer<'_>>,
) -> Result<Option<Timestamp>, DiagnosticsStreamError> {
    Ok(match &parameters.min_timestamp_nanos {
        Some(TimeBound::Absolute(t)) => {
            if let Some(session) = stream.session_timestamp_nanos().await {
                Some(Timestamp::from(*t as i64 - session))
            } else {
                None
            }
        }
        Some(TimeBound::Monotonic(t)) => Some(Timestamp::from(*t as i64)),
        Some(bound) => {
            log::error!("Got unexpected TimeBound field {:?}", bound);
            return Err(DiagnosticsStreamError::GenericError);
        }
        None => parameters.min_target_timestamp_nanos.map(|t| Timestamp::from(t as i64)),
    })
}
