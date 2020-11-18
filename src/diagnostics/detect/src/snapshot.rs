// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Triggers a snapshot via FIDL

use log::{error, warn};

// Name of the crash-report product we're filing against.
const CRASH_PRODUCT_NAME: &'static str = "FuchsiaDetect";
// CRASH_PROGRAM_NAME serves two purposes:
// 1) It is sent with the crash report. It may show up on the server as
//   "process type".
// 2) The on-device crash reporting program associates this string with the
//   "product" CRASH_PRODUCT_NAME we're requesting to file against, so we
//   only have to send the program name and not the product name with each
//   crash report request.
//   This association is registered via a call to
//   CrashReportingProductRegister.upsert().
const CRASH_PROGRAM_NAME: &str = "triage_detect";

#[derive(Debug)]
pub struct SnapshotRequest {
    signature: String,
}

impl SnapshotRequest {
    pub fn new(signature: String) -> SnapshotRequest {
        SnapshotRequest { signature }
    }
}

// Code shamelessly stolen and slightly adapted from
// garnet/bin/power_manager/src/crash_report_handler.rs

use anyhow::{anyhow, format_err, Error};
//use async_trait::async_trait;
use fidl_fuchsia_feedback as fidl_feedback;
use fuchsia_async as fasync;
use futures::channel::mpsc;
use futures::stream::StreamExt;
use std::cell::RefCell;
use std::rc::Rc;

/// CrashReportHandler
///
/// Summary: Provides a mechanism for filing crash reports.
///
/// FIDL dependencies:
///     - fuchsia.feedback.CrashReporter: CrashReportHandler uses this protocol to communicate
///       with the CrashReporter service in order to file crash reports.
///     - fuchsia.feedback.CrashReportingProductRegister: CrashReportHandler uses this protocol
///       to communicate with the CrashReportingProductRegister service in order to configure
///       the crash reporting product it will be filing on.
///

/// Path to the CrashReporter service.
const CRASH_REPORTER_SVC: &'static str = "/svc/fuchsia.feedback.CrashReporter";
/// Path to the CrashReportingProductRegister service.
const CRASH_REGISTER_SVC: &'static str = "/svc/fuchsia.feedback.CrashReportingProductRegister";

/// The maximum number of pending crash report requests. This is needed because the FIDL API to file
/// a crash report does not return until the crash report has been fully generated, which can take
/// many seconds. Supporting pending crash reports means Detect can file
/// a new crash report for any other reason within that window, but the CrashReportHandler will
/// handle rate limiting to the CrashReporter service.
const MAX_PENDING_CRASH_REPORTS: usize = 10;

/// A builder for constructing the CrashReportHandler node.
pub struct CrashReportHandlerBuilder {
    proxy: Option<fidl_feedback::CrashReporterProxy>,
    max_pending_crash_reports: usize,
}

// This function is from fuchsia-mirror/garnet/bin/power_manager/src/utils.rs
/// Create and connect a FIDL proxy to the service at `path`
fn connect_proxy<T: fidl::endpoints::ServiceMarker>(
    path: &String,
) -> Result<T::Proxy, anyhow::Error> {
    let (proxy, server) = fidl::endpoints::create_proxy::<T>()
        .map_err(|e| anyhow::format_err!("Failed to create proxy: {}", e))?;

    fdio::service_connect(path, server.into_channel())
        .map_err(|s| anyhow::format_err!("Failed to connect to service at {}: {}", path, s))?;
    Ok(proxy)
}

/// Logs an error message if the passed in `result` is an error.
#[macro_export]
macro_rules! log_if_err {
    ($result:expr, $log_prefix:expr) => {
        if let Err(e) = $result.as_ref() {
            log::error!("{}: {}", $log_prefix, e);
        }
    };
}

impl CrashReportHandlerBuilder {
    pub fn new() -> Self {
        Self { proxy: None, max_pending_crash_reports: MAX_PENDING_CRASH_REPORTS }
    }

    #[cfg(test)]
    pub fn with_proxy(mut self, proxy: fidl_feedback::CrashReporterProxy) -> Self {
        self.proxy = Some(proxy);
        self
    }

    #[cfg(test)]
    pub fn with_max_pending_crash_reports(mut self, max: usize) -> Self {
        self.max_pending_crash_reports = max;
        self
    }

    pub fn build(self) -> Result<Rc<CrashReportHandler>, Error> {
        // Proxy is only pre-set for tests. If a proxy was not specified,
        // this is a good time to configure for our crash reporting product.
        if matches!(self.proxy, None) {
            let config_proxy = connect_proxy::<fidl_feedback::CrashReportingProductRegisterMarker>(
                &CRASH_REGISTER_SVC.to_string(),
            )?;
            let product_config = fidl_feedback::CrashReportingProduct {
                name: Some(CRASH_PRODUCT_NAME.to_string()),
                ..fidl_feedback::CrashReportingProduct::empty()
            };
            config_proxy.upsert(&CRASH_PROGRAM_NAME.to_string(), product_config)?;
        }
        // Connect to the CrashReporter service if a proxy wasn't specified
        let proxy = if self.proxy.is_some() {
            self.proxy.unwrap()
        } else {
            connect_proxy::<fidl_feedback::CrashReporterMarker>(&CRASH_REPORTER_SVC.to_string())?
        };

        // Set up the crash report sender that runs asynchronously
        let (channel, receiver) = mpsc::channel(self.max_pending_crash_reports);
        CrashReportHandler::begin_crash_report_sender(proxy, receiver);

        Ok(Rc::new(CrashReportHandler {
            channel_size: self.max_pending_crash_reports,
            crash_report_sender: RefCell::new(channel),
        }))
    }
}

pub struct CrashReportHandler {
    /// The channel to send new crash report requests to the asynchronous crash report sender
    /// future. The maximum pending crash reports are implicitly enforced by the channel length.
    crash_report_sender: RefCell<mpsc::Sender<SnapshotRequest>>,
    channel_size: usize,
}

impl CrashReportHandler {
    /// Handle a FileCrashReport message by sending the specified crash report signature over the
    /// channel to the crash report sender.
    pub fn request_snapshot(&self, request: SnapshotRequest) -> Result<(), Error> {
        // Try to send the crash report signature over the channel. If the channel is full, return
        // an error
        match self.crash_report_sender.borrow_mut().try_send(request) {
            Ok(()) => Ok(()),
            Err(e) if e.is_full() => {
                warn!("Too many crash reports pending: {}", e);
                Err(anyhow!("Pending crash reports exceeds max ({})", self.channel_size))
            }
            Err(e) => {
                warn!("Error sending crash report: {}", e);
                Err(anyhow!("{}", e))
            }
        }
    }

    /// Spawn and detach a future that receives crash report signatures over the channel and uses
    /// the proxy to send a File FIDL request to the CrashReporter service with the specified
    /// signatures.
    fn begin_crash_report_sender(
        proxy: fidl_feedback::CrashReporterProxy,
        mut receive_channel: mpsc::Receiver<SnapshotRequest>,
    ) {
        fasync::Task::local(async move {
            while let Some(request) = receive_channel.next().await {
                log_if_err!(
                    Self::send_crash_report(&proxy, request).await,
                    "Failed to file crash report"
                );
            }
            error!("Crash reporter task ended. Crash reports will no longer be filed. This should not happen.")
        })
        .detach();
    }

    /// Send a File request to the CrashReporter service with the specified crash report signature.
    async fn send_crash_report(
        proxy: &fidl_feedback::CrashReporterProxy,
        payload: SnapshotRequest,
    ) -> Result<(), Error> {
        warn!("Filing crash report, signature '{}'", payload.signature);
        let report = fidl_feedback::CrashReport {
            program_name: Some(CRASH_PROGRAM_NAME.to_string()),
            specific_report: Some(fidl_feedback::SpecificCrashReport::Generic(
                fidl_feedback::GenericCrashReport {
                    crash_signature: Some(payload.signature),
                    ..fidl_feedback::GenericCrashReport::empty()
                },
            )),
            ..fidl_feedback::CrashReport::empty()
        };

        let result = proxy.file(report).await.map_err(|e| format_err!("IPC error: {}", e))?;
        result.map_err(|e| format_err!("Service error: {}", e))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::TryStreamExt;
    use matches::assert_matches;

    /// Tests that the node responds to the FileCrashReport message and that the expected crash
    /// report is received by the CrashReporter service.
    #[fasync::run_singlethreaded(test)]
    async fn test_crash_report_content() {
        // The crash report signature to use and verify against
        let crash_report_signature = "TestCrashReportSignature";

        // Set up the CrashReportHandler node
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_feedback::CrashReporterMarker>()
                .unwrap();
        let crash_report_handler =
            CrashReportHandlerBuilder::new().with_proxy(proxy).build().unwrap();

        // File a crash report
        crash_report_handler
            .request_snapshot(SnapshotRequest::new(crash_report_signature.to_string()))
            .unwrap();

        // Verify the fake service receives the crash report with expected data
        if let Ok(Some(fidl_feedback::CrashReporterRequest::File { responder: _, report })) =
            stream.try_next().await
        {
            assert_eq!(
                report,
                fidl_feedback::CrashReport {
                    program_name: Some(CRASH_PROGRAM_NAME.to_string()),
                    specific_report: Some(fidl_feedback::SpecificCrashReport::Generic(
                        fidl_feedback::GenericCrashReport {
                            crash_signature: Some(crash_report_signature.to_string()),
                            ..fidl_feedback::GenericCrashReport::empty()
                        },
                    )),
                    ..fidl_feedback::CrashReport::empty()
                }
            );
        } else {
            panic!("Did not receive a crash report");
        }
    }

    /// Tests that the number of pending crash reports is correctly bounded.
    #[test]
    fn test_crash_report_pending_reports() {
        let mut exec = fasync::Executor::new().unwrap();

        // Set up the proxy/stream and node outside of the large future used below. This way we can
        // still poll the stream after the future completes.
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_feedback::CrashReporterMarker>()
                .unwrap();
        let crash_report_handler = CrashReportHandlerBuilder::new()
            .with_proxy(proxy)
            .with_max_pending_crash_reports(1)
            .build()
            .unwrap();

        // Run most of the test logic inside a top level future for better ergonomics
        exec.run_singlethreaded(async {
            // Set up the CrashReportHandler node. The request stream is never serviced, so when the
            // node makes the FIDL call to file the crash report, the call will block indefinitely.
            // This lets us test the pending crash report counts.

            // The first FileCrashReport should succeed
            assert_matches!(
                crash_report_handler
                    .request_snapshot(SnapshotRequest::new("TestCrash1".to_string())),
                Ok(())
            );

            // The second FileCrashReport should also succeed because since the first is now in
            // progress, this is now the first "pending" report request
            assert_matches!(
                crash_report_handler
                    .request_snapshot(SnapshotRequest::new("TestCrash2".to_string())),
                Ok(())
            );

            // Since the first request has not completed, and there is already one pending request,
            // this request should fail
            assert_matches!(
                crash_report_handler
                    .request_snapshot(SnapshotRequest::new("TestCrash3".to_string())),
                Err(_)
            );

            // Verify the signature of the first crash report
            if let Ok(Some(fidl_feedback::CrashReporterRequest::File { responder, report })) =
                stream.try_next().await
            {
                // Send a reply to allow the node to process the next crash report
                let _ = responder.send(&mut Ok(()));
                assert_eq!(
                    report,
                    fidl_feedback::CrashReport {
                        program_name: Some(CRASH_PROGRAM_NAME.to_string()),
                        specific_report: Some(fidl_feedback::SpecificCrashReport::Generic(
                            fidl_feedback::GenericCrashReport {
                                crash_signature: Some("TestCrash1".to_string()),
                                ..fidl_feedback::GenericCrashReport::empty()
                            },
                        )),
                        ..fidl_feedback::CrashReport::empty()
                    }
                );
            } else {
                panic!("Did not receive a crash report");
            }

            // Verify the signature of the second crash report
            if let Ok(Some(fidl_feedback::CrashReporterRequest::File { responder, report })) =
                stream.try_next().await
            {
                // Send a reply to allow the node to process the next crash report
                let _ = responder.send(&mut Ok(()));
                assert_eq!(
                    report,
                    fidl_feedback::CrashReport {
                        program_name: Some(CRASH_PROGRAM_NAME.to_string()),
                        specific_report: Some(fidl_feedback::SpecificCrashReport::Generic(
                            fidl_feedback::GenericCrashReport {
                                crash_signature: Some("TestCrash2".to_string()),
                                ..fidl_feedback::GenericCrashReport::empty()
                            },
                        )),
                        ..fidl_feedback::CrashReport::empty()
                    }
                );
            } else {
                panic!("Did not receive a crash report");
            }
        });

        // Verify there are no more crash reports. Use `run_until_stalled` because `next` is
        // expected to block until a new crash report is ready, which shouldn't happen here.
        assert!(exec.run_until_stalled(&mut stream.next()).is_pending());
    }
}
