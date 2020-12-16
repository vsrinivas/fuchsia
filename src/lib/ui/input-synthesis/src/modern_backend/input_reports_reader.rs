// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_input_report::{
        InputReport, InputReportsReaderRequest, InputReportsReaderRequestStream,
    },
    futures::{stream, StreamExt, TryStreamExt},
    std::convert::TryFrom as _,
};

/// Implements the server side of the `fuchsia.input.report.InputReportsReader`
/// protocol. Used by `modern_backend::InputDevice`.
pub(super) struct InputReportsReader {
    pub(super) request_stream: InputReportsReaderRequestStream,
    /// FIFO queue of reports to be consumed by calls to
    /// `fuchsia.input.report.InputReportsReader.ReadInputReports()`.
    pub(super) reports: Vec<InputReport>,
}

impl InputReportsReader {
    /// Returns a `Future` that resolves when
    /// * `self.reports` is empty, or
    /// * `self.request_stream` yields `None`, or
    /// * an error occurs (invalid FIDL request, failure to send FIDL response).
    ///
    /// # Resolves to
    /// * `Ok(())` if all reports were written successfully
    /// * `Err` otherwise
    ///
    /// # Corner cases
    /// If `self.reports` is _initially_ empty, the returned `Future` will resolve immediately.
    ///
    /// # Note
    /// When the future resolves, `InputReports` may still be sitting unread in the
    /// channel to the `fuchsia.input.report.InputReportsReader` client. (The client will
    /// typically be an input pipeline implementation.)
    pub(super) async fn into_future(self) -> Result<(), Error> {
        // Group `reports` into chunks, to respect the requirements of the `InputReportsReader`
        // protocol. Then `zip()` each chunk with a `InputReportsReader` protocol request.
        // * If there are more chunks than requests, then some of the `InputReport`s were
        //   not sent to the `InputReportsReader` client. In this case, this function
        //   will report an error by checking `reports.is_done()` below.
        // * If there are more requests than reports, no special-case handling is needed.
        //   This is because an input pipeline implementation will normally issue
        //   `ReadInputReports` requests indefinitely.
        let chunk_size = usize::try_from(fidl_fuchsia_input_report::MAX_DEVICE_REPORT_COUNT)
            .context("converting MAX_DEVICE_REPORT_COUNT to usize")?;
        let mut reports = stream::iter(self.reports).chunks(chunk_size).fuse();
        self.request_stream
            .zip(reports.by_ref())
            .map(|(request, reports)| match request {
                Ok(request) => Ok((request, reports)),
                Err(e) => Err(anyhow::Error::from(e).context("while reading reader request")),
            })
            .try_for_each(|request_and_reports| async {
                match request_and_reports {
                    (InputReportsReaderRequest::ReadInputReports { responder }, reports) => {
                        responder
                            .send(&mut Ok(reports))
                            .map_err(anyhow::Error::from)
                            .context("while sending reports")
                    }
                }
            })
            .await?;

        match reports.is_done() {
            true => Ok(()),
            false => Err(format_err!("request_stream terminated with reports still pending")),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{InputReport, InputReportsReader},
        anyhow::{Context as _, Error},
        fidl::endpoints,
        fidl_fuchsia_input_report::{InputReportsReaderMarker, MAX_DEVICE_REPORT_COUNT},
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::future,
        matches::assert_matches,
    };

    mod report_count {
        use {
            super::*,
            futures::{pin_mut, task::Poll},
            std::convert::TryFrom,
        };

        #[fasync::run_until_stalled(test)]
        async fn serves_single_report() -> Result<(), Error> {
            let (proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut =
                InputReportsReader { request_stream, reports: vec![InputReport::EMPTY] }
                    .into_future();
            let reports_fut = proxy.read_input_reports();
            std::mem::drop(proxy); // Drop `proxy` to terminate `request_stream`.

            let (_, reports_result) = future::join(reader_fut, reports_fut).await;
            let reports = reports_result
                .expect("fidl error")
                .map_err(zx::Status::from_raw)
                .expect("service error");
            assert_eq!(reports.len(), 1, "incorrect reports length");
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn serves_max_report_count_reports() -> Result<(), Error> {
            let max_reports = usize::try_from(MAX_DEVICE_REPORT_COUNT)
                .context("internal error converting MAX_DEVICE_REPORT_COUNT to usize")?;
            let (proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut = InputReportsReader {
                request_stream,
                reports: std::iter::repeat_with(|| InputReport::EMPTY).take(max_reports).collect(),
            }
            .into_future();
            let reports_fut = proxy.read_input_reports();
            std::mem::drop(proxy); // Drop `proxy` to terminate `request_stream`.

            let (_, reports_result) = future::join(reader_fut, reports_fut).await;
            let reports = reports_result
                .expect("fidl error")
                .map_err(zx::Status::from_raw)
                .expect("service error");
            assert_eq!(reports.len(), max_reports, "incorrect reports length");
            Ok(())
        }

        #[test]
        fn splits_overflowed_reports_to_next_read() -> Result<(), Error> {
            let mut executor = fasync::Executor::new().expect("creating executor");
            let max_reports = usize::try_from(MAX_DEVICE_REPORT_COUNT)
                .context("internal error converting MAX_DEVICE_REPORT_COUNT to usize")?;
            let (proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut = InputReportsReader {
                request_stream,
                reports: std::iter::repeat_with(|| InputReport::EMPTY)
                    .take(max_reports + 1)
                    .collect(),
            }
            .into_future();
            pin_mut!(reader_fut);

            // Note: this test deliberately serializes its FIDL requests. Concurrent requests
            // are tested separately, in `super::fidl_interactions::preserves_query_order()`.
            let reports_fut = proxy.read_input_reports();
            let _ = executor.run_until_stalled(&mut reader_fut);
            pin_mut!(reports_fut);
            match executor.run_until_stalled(&mut reports_fut) {
                Poll::Pending => panic!("read did not complete (1st query)"),
                Poll::Ready(res) => {
                    let reports = res
                        .expect("fidl error")
                        .map_err(zx::Status::from_raw)
                        .expect("service error");
                    assert_eq!(reports.len(), max_reports, "incorrect reports length (1st query)");
                }
            }

            let reports_fut = proxy.read_input_reports();
            let _ = executor.run_until_stalled(&mut reader_fut);
            pin_mut!(reports_fut);
            match executor.run_until_stalled(&mut reports_fut) {
                Poll::Pending => panic!("read did not complete (2nd query)"),
                Poll::Ready(res) => {
                    let reports = res
                        .expect("fidl error")
                        .map_err(zx::Status::from_raw)
                        .expect("service error");
                    assert_eq!(reports.len(), 1, "incorrect reports length (2nd query)");
                }
            }

            Ok(())
        }
    }

    mod future_resolution {
        use super::*;

        #[fasync::run_until_stalled(test)]
        async fn resolves_to_ok_when_all_reports_are_written() -> Result<(), Error> {
            let (proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut =
                InputReportsReader { request_stream, reports: vec![InputReport::EMPTY] }
                    .into_future();
            let _reports_fut = proxy.read_input_reports();
            assert_matches!(reader_fut.await, Ok(()));
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn resolves_to_err_when_request_stream_is_terminated_before_reports_are_written(
        ) -> Result<(), Error> {
            let (proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut =
                InputReportsReader { request_stream, reports: vec![InputReport::EMPTY] }
                    .into_future();
            std::mem::drop(proxy); // Drop `proxy` to terminate `request_stream`.
            assert_matches!(reader_fut.await, Err(_));
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn resolves_to_err_if_request_stream_yields_error() -> Result<(), Error> {
            let (client_end, request_stream) =
                endpoints::create_request_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader client_end and stream")?;
            let reader_fut =
                InputReportsReader { request_stream, reports: vec![InputReport::EMPTY] }
                    .into_future();
            client_end
                .into_channel()
                .write(b"not a valid FIDL message", /* handles */ &mut [])
                .expect("internal error writing to channel");
            assert_matches!(reader_fut.await, Err(_));
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn resolves_to_err_if_send_fails() -> Result<(), Error> {
            let (proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut =
                InputReportsReader { request_stream, reports: vec![InputReport::EMPTY] }
                    .into_future();
            let result_fut = proxy.read_input_reports(); // Send query.
            std::mem::drop(result_fut); // Close handle to channel.
            std::mem::drop(proxy); // Close other handle to channel.
            assert_matches!(reader_fut.await, Err(_));
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn immediately_resolves_to_ok_when_reports_is_initially_empty() -> Result<(), Error> {
            let (_proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut = InputReportsReader { request_stream, reports: vec![] }.into_future();
            assert_matches!(reader_fut.await, Ok(()));
            Ok(())
        }
    }

    mod fidl_interactions {
        use {
            super::*,
            futures::{pin_mut, task::Poll},
            std::convert::TryFrom,
        };

        #[test]
        fn closes_channel_after_reports_are_consumed() -> Result<(), Error> {
            let mut executor = fasync::Executor::new().expect("creating executor");
            let (proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut =
                InputReportsReader { request_stream, reports: vec![InputReport::EMPTY] }
                    .into_future();
            let reports_fut = proxy.read_input_reports();

            // Process the first query. This should close the FIDL connection.
            let futures = future::join(reader_fut, reports_fut);
            pin_mut!(futures);
            std::mem::drop(executor.run_until_stalled(&mut futures));

            // Try sending another query. This should fail.
            assert_matches!(
                executor.run_until_stalled(&mut proxy.read_input_reports()),
                Poll::Ready(Err(fidl::Error::ClientChannelClosed { .. }))
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn preserves_query_order() -> Result<(), Error> {
            let max_reports = usize::try_from(MAX_DEVICE_REPORT_COUNT)
                .context("internal error converting MAX_DEVICE_REPORT_COUNT to usize")?;
            let (proxy, request_stream) =
                endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and stream")?;
            let reader_fut = InputReportsReader {
                request_stream,
                reports: std::iter::repeat_with(|| InputReport::EMPTY)
                    .take(max_reports + 1)
                    .collect(),
            }
            .into_future();
            let first_reports_fut = proxy.read_input_reports();
            let second_reports_fut = proxy.read_input_reports();
            std::mem::drop(proxy); // Drop `proxy` to terminate `request_stream`.

            let (_, first_reports_result, second_reports_result) =
                futures::join!(reader_fut, first_reports_fut, second_reports_fut);
            let first_reports = first_reports_result
                .expect("fidl error")
                .map_err(zx::Status::from_raw)
                .expect("service error");
            let second_reports = second_reports_result
                .expect("fidl error")
                .map_err(zx::Status::from_raw)
                .expect("service error");
            assert_eq!(first_reports.len(), max_reports, "incorrect reports length (1st query)");
            assert_eq!(second_reports.len(), 1, "incorrect reports length (2nd query)");
            Ok(())
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn preserves_report_order() -> Result<(), Error> {
        let (proxy, request_stream) =
            endpoints::create_proxy_and_stream::<InputReportsReaderMarker>()
                .context("creating InputReportsReader proxy and stream")?;
        let reader_fut = InputReportsReader {
            request_stream,
            reports: vec![
                InputReport { event_time: Some(1), ..InputReport::EMPTY },
                InputReport { event_time: Some(2), ..InputReport::EMPTY },
            ],
        }
        .into_future();
        let reports_fut = proxy.read_input_reports();
        assert_eq!(
            future::join(reader_fut, reports_fut)
                .await
                .1
                .expect("fidl error")
                .map_err(zx::Status::from_raw)
                .expect("service error")
                .iter()
                .map(|report| report.event_time)
                .collect::<Vec<_>>(),
            [Some(1), Some(2)]
        );
        Ok(())
    }
}
