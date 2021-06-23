// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    diagnostics_data::LogsData,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorMarker, ArchiveIteratorProxy, DiagnosticsData,
    },
    fidl_fuchsia_test_manager as ftest_manager, fuchsia_async as fasync,
    futures::Stream,
    futures::{channel::mpsc, stream::BoxStream, SinkExt, StreamExt},
    pin_project::pin_project,
    serde_json,
    std::{
        pin::Pin,
        task::{Context, Poll},
    },
};

#[cfg(target_os = "fuchsia")]
use crate::diagnostics::fuchsia::BatchLogStream;

#[pin_project]
pub struct LogStream {
    #[pin]
    stream: BoxStream<'static, Result<LogsData, Error>>,
}

impl LogStream {
    fn new<S>(stream: S) -> Self
    where
        S: Stream<Item = Result<LogsData, Error>> + Send + 'static,
    {
        Self { stream: stream.boxed() }
    }

    pub fn from_syslog(syslog: ftest_manager::Syslog) -> Result<LogStream, fidl::Error> {
        get_log_stream(syslog)
    }
}

impl Stream for LogStream {
    type Item = Result<LogsData, Error>;
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = self.project();
        this.stream.poll_next(cx)
    }
}

#[cfg(target_os = "fuchsia")]
fn get_log_stream(syslog: ftest_manager::Syslog) -> Result<LogStream, fidl::Error> {
    match syslog {
        ftest_manager::Syslog::Archive(client_end) => {
            Ok(LogStream::new(ArchiveLogStream::from_client_end(client_end)?))
        }
        ftest_manager::Syslog::Batch(client_end) => {
            Ok(LogStream::new(BatchLogStream::from_client_end(client_end)?))
        }
        _ => {
            panic!("not supported")
        }
    }
}

#[cfg(not(target_os = "fuchsia"))]
fn get_log_stream(syslog: ftest_manager::Syslog) -> Result<LogStream, fidl::Error> {
    match syslog {
        ftest_manager::Syslog::Archive(client_end) => {
            Ok(LogStream::new(ArchiveLogStream::from_client_end(client_end)?))
        }
        ftest_manager::Syslog::Batch(_) => panic!("batch iterator not supported on host"),
        _ => {
            panic!("not supported")
        }
    }
}

#[cfg(target_os = "fuchsia")]
mod fuchsia {
    use {
        super::*, diagnostics_data::Logs, diagnostics_reader::Subscription,
        fidl_fuchsia_diagnostics::BatchIteratorMarker,
    };

    #[pin_project]
    pub struct BatchLogStream {
        #[pin]
        subscription: Subscription<Logs>,
    }

    impl BatchLogStream {
        #[cfg(test)]
        pub fn new() -> Result<(Self, ftest_manager::LogsIterator), fidl::Error> {
            fidl::endpoints::create_proxy::<BatchIteratorMarker>().map(|(proxy, server_end)| {
                let subscription = Subscription::new(proxy);
                (Self { subscription }, ftest_manager::LogsIterator::Batch(server_end))
            })
        }

        pub fn from_client_end(
            client_end: ClientEnd<BatchIteratorMarker>,
        ) -> Result<Self, fidl::Error> {
            Ok(Self { subscription: Subscription::new(client_end.into_proxy()?) })
        }
    }

    impl Stream for BatchLogStream {
        type Item = Result<LogsData, Error>;
        fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            let this = self.project();
            match this.subscription.poll_next(cx) {
                Poll::Ready(Some(Err(e))) => Poll::Ready(Some(Err(e.into()))),
                Poll::Ready(Some(Ok(value))) => Poll::Ready(Some(Ok(value))),
                Poll::Ready(None) => Poll::Ready(None),
                Poll::Pending => Poll::Pending,
            }
        }
    }
}

#[pin_project]
struct ArchiveLogStream {
    #[pin]
    receiver: mpsc::Receiver<Result<LogsData, Error>>,
    _drain_task: fasync::Task<()>,
}

impl ArchiveLogStream {
    #[cfg(test)]
    #[cfg(not(target_os = "fuchsia"))]
    fn new() -> Result<(Self, ftest_manager::LogsIterator), fidl::Error> {
        fidl::endpoints::create_proxy::<ArchiveIteratorMarker>().map(|(proxy, server_end)| {
            let (receiver, _drain_task) = Self::start_streaming_logs(proxy);
            (Self { _drain_task, receiver }, ftest_manager::LogsIterator::Archive(server_end))
        })
    }

    pub fn from_client_end(
        client_end: ClientEnd<ArchiveIteratorMarker>,
    ) -> Result<Self, fidl::Error> {
        let (receiver, _drain_task) = Self::start_streaming_logs(client_end.into_proxy()?);
        Ok(Self { _drain_task, receiver })
    }
}

impl ArchiveLogStream {
    fn start_streaming_logs(
        proxy: ArchiveIteratorProxy,
    ) -> (mpsc::Receiver<Result<LogsData, Error>>, fasync::Task<()>) {
        let (mut sender, receiver) = mpsc::channel(32);
        let task = fasync::Task::spawn(async move {
            loop {
                let result = match proxy.get_next().await {
                    Err(e) => {
                        let _ =
                            sender.send(Err(format_err!("Error calling GetNext: {:?}", e))).await;
                        break;
                    }
                    Ok(batch) => batch,
                };

                let entries = match result {
                    Err(e) => {
                        let _ =
                            sender.send(Err(format_err!("GetNext returned error: {:?}", e))).await;
                        break;
                    }
                    Ok(entries) => entries,
                };

                if entries.is_empty() {
                    break;
                }

                for diagnostics_data in
                    entries.into_iter().map(|e| e.diagnostics_data).filter_map(|data| data)
                {
                    match diagnostics_data {
                        DiagnosticsData::Inline(inline) => {
                            let _ = match serde_json::from_str(&inline.data) {
                                Ok(data) => sender.send(Ok(data)).await,
                                Err(e) => {
                                    sender.send(Err(format_err!("Malformed json: {:?}", e))).await
                                }
                            };
                        }
                        _ => {}
                    }
                }
            }
        });
        (receiver, task)
    }
}

impl Stream for ArchiveLogStream {
    type Item = Result<LogsData, Error>;
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = self.project();
        this.receiver.poll_next(cx)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        diagnostics_data::{Severity, Timestamp},
        fidl::endpoints::ServerEnd,
        fuchsia_async as fasync,
        futures::TryStreamExt,
    };

    #[cfg(target_os = "fuchsia")]
    mod fuchsia {
        use {
            super::*,
            fidl_fuchsia_diagnostics::{
                BatchIteratorMarker, BatchIteratorRequest, FormattedContent, ReaderError,
            },
            fidl_fuchsia_mem as fmem, fuchsia_zircon as zx,
            futures::StreamExt,
            matches::assert_matches,
        };

        fn create_log_stream() -> Result<(LogStream, ftest_manager::LogsIterator), fidl::Error> {
            let (stream, iterator) = BatchLogStream::new()?;
            Ok((LogStream::new(stream), iterator))
        }

        struct BatchIteratorOpts {
            with_error: bool,
        }

        /// Spanws a dummy batch iterator for testing that return 3 logs: "1", "2", "3" all with
        /// the same severity
        async fn spawn_batch_iterator_server(
            server_end: ServerEnd<BatchIteratorMarker>,
            opts: BatchIteratorOpts,
        ) {
            let mut request_stream = server_end.into_stream().expect("got stream");
            let mut values = vec![1i64, 2, 3].into_iter();
            while let Some(BatchIteratorRequest::GetNext { responder }) =
                request_stream.try_next().await.expect("get next request")
            {
                match values.next() {
                    None => {
                        responder.send(&mut Ok(vec![])).expect("send empty response");
                    }
                    Some(value) => {
                        if opts.with_error {
                            responder.send(&mut Err(ReaderError::Io)).expect("send error");
                            continue;
                        }
                        let content = get_json_data(value);
                        let size = content.len() as u64;
                        let vmo = zx::Vmo::create(size).expect("create vmo");
                        vmo.write(content.as_bytes(), 0).expect("write vmo");
                        let result = FormattedContent::Json(fmem::Buffer { vmo, size });
                        responder.send(&mut Ok(vec![result])).expect("send response");
                    }
                }
            }
        }

        #[fasync::run_singlethreaded(test)]
        async fn log_stream_returns_logs() {
            let (mut log_stream, iterator) = create_log_stream().expect("got log stream");
            let server_end = match iterator {
                ftest_manager::LogsIterator::Batch(server_end) => server_end,
                _ => panic!("unexpected logs iterator server end"),
            };
            fasync::Task::spawn(spawn_batch_iterator_server(
                server_end,
                BatchIteratorOpts { with_error: false },
            ))
            .detach();
            assert_eq!(log_stream.next().await.unwrap().expect("got ok result").msg(), Some("1"));
            assert_eq!(log_stream.next().await.unwrap().expect("got ok result").msg(), Some("2"));
            assert_eq!(log_stream.next().await.unwrap().expect("got ok result").msg(), Some("3"));
            assert_matches!(log_stream.next().await, None);
        }

        #[fasync::run_singlethreaded(test)]
        async fn log_stream_can_return_errors() {
            let (mut log_stream, iterator) = create_log_stream().expect("got log stream");
            let server_end = match iterator {
                ftest_manager::LogsIterator::Batch(server_end) => server_end,
                _ => panic!("unexpected logs iterator server end"),
            };
            fasync::Task::spawn(spawn_batch_iterator_server(
                server_end,
                BatchIteratorOpts { with_error: true },
            ))
            .detach();
            assert_matches!(log_stream.next().await, Some(Err(_)));
        }
    }

    #[cfg(not(target_os = "fuchsia"))]
    mod host {
        use {
            super::*,
            fidl_fuchsia_developer_remotecontrol::{
                ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker,
                ArchiveIteratorRequest, DiagnosticsData, InlineData,
            },
            matches::assert_matches,
        };

        fn create_log_stream() -> Result<(LogStream, ftest_manager::LogsIterator), fidl::Error> {
            let (stream, iterator) = ArchiveLogStream::new()?;
            Ok((LogStream::new(stream), iterator))
        }

        async fn spawn_archive_iterator_server(
            server_end: ServerEnd<ArchiveIteratorMarker>,
            with_error: bool,
        ) {
            let mut request_stream = server_end.into_stream().expect("got stream");
            let mut values = vec![1, 2, 3].into_iter();
            while let Some(ArchiveIteratorRequest::GetNext { responder }) =
                request_stream.try_next().await.expect("get next request")
            {
                match values.next() {
                    None => {
                        responder.send(&mut Ok(vec![])).expect("send empty response");
                    }
                    Some(value) => {
                        if with_error {
                            responder
                                .send(&mut Err(ArchiveIteratorError::DataReadFailed))
                                .expect("send error");
                            continue;
                        }
                        let json_data = get_json_data(value);
                        let result = ArchiveIteratorEntry {
                            diagnostics_data: Some(DiagnosticsData::Inline(InlineData {
                                data: json_data,
                                truncated_chars: 0,
                            })),
                            ..ArchiveIteratorEntry::EMPTY
                        };
                        responder.send(&mut Ok(vec![result])).expect("send response");
                    }
                }
            }
        }

        #[fasync::run_singlethreaded(test)]
        async fn archive_stream_returns_logs() {
            let (mut log_stream, iterator) = create_log_stream().expect("got log stream");
            let server_end = match iterator {
                ftest_manager::LogsIterator::Archive(server_end) => server_end,
                _ => panic!("unexpected logs iterator server end"),
            };
            fasync::Task::spawn(spawn_archive_iterator_server(server_end, false)).detach();
            assert_eq!(log_stream.next().await.unwrap().expect("got ok result").msg(), Some("1"));
            assert_eq!(log_stream.next().await.unwrap().expect("got ok result").msg(), Some("2"));
            assert_eq!(log_stream.next().await.unwrap().expect("got ok result").msg(), Some("3"));
            assert_matches!(log_stream.next().await, None);
        }

        #[fasync::run_singlethreaded(test)]
        async fn archive_stream_can_return_errors() {
            let (mut log_stream, iterator) = create_log_stream().expect("got log stream");
            let server_end = match iterator {
                ftest_manager::LogsIterator::Archive(server_end) => server_end,
                _ => panic!("unexpected logs iterator server end"),
            };
            fasync::Task::spawn(spawn_archive_iterator_server(server_end, true)).detach();
            assert_matches!(log_stream.next().await, Some(Err(_)));
        }
    }

    fn get_json_data(value: i64) -> String {
        let data = diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
            timestamp_nanos: Timestamp::from(0).into(),
            component_url: String::from("fake-url"),
            moniker: String::from("test/moniker"),
            severity: Severity::Info,
            size_bytes: 1,
        })
        .set_message(value.to_string())
        .build();

        serde_json::to_string(&data).expect("serialize to json")
    }
}
