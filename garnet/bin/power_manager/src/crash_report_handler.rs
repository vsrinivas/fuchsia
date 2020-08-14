// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::utils::connect_proxy;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_feedback as fidl_feedback;
use fuchsia_async as fasync;
use futures::channel::mpsc;
use futures::stream::StreamExt;
use std::cell::RefCell;
use std::rc::Rc;

/// Node: CrashReportHandler
///
/// Summary: Provides a mechanism for filing crash reports.
///
/// Handles Messages:
///     - FileCrashReport
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fuchsia.feedback.CrashReporter: the node uses this protocol to communicate with the
///       CrashReporter service in order to file crash reports.
///

/// Path to the CrashReporter service.
const CRASH_REPORTER_SVC: &'static str = "/svc/fuchsia.feedback.CrashReporter";

/// The maximum number of pending crash report requests. This is needed because the FIDL API to file
/// a crash report does not return until the crash report has been fully generated, which can take
/// upwards of 30 seconds or more. Supporting pending crash reports means the PowerManager can file
/// a new crash report for any other reason within that window, but the CrashReportHandler will
/// handle rate limiting to the CrashReporter service.
const MAX_PENDING_CRASH_REPORTS: usize = 5;

/// A builder for constructing the CrashReportHandler node.
pub struct CrashReportHandlerBuilder {
    proxy: Option<fidl_feedback::CrashReporterProxy>,
    max_pending_crash_reports: usize,
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
        // Connect to the CrashReporter service if a proxy wasn't specified
        let proxy = if self.proxy.is_some() {
            self.proxy.unwrap()
        } else {
            connect_proxy::<fidl_feedback::CrashReporterMarker>(&CRASH_REPORTER_SVC.to_string())?
        };

        // Set up the crash report sender that runs asynchronously
        let (channel, receiver) = mpsc::channel(self.max_pending_crash_reports);
        CrashReportHandler::begin_crash_report_sender(proxy, receiver);

        Ok(Rc::new(CrashReportHandler { crash_report_sender: RefCell::new(channel) }))
    }
}

pub struct CrashReportHandler {
    /// The channel to send new crash report requests to the asynchronous crash report sender
    /// future. The maximum pending crash reports are implicitly enforced by the channel length.
    crash_report_sender: RefCell<mpsc::Sender<String>>,
}

impl CrashReportHandler {
    /// Default name to use for `program_name` in the crash report. Using "device" here to align
    /// with other device/hardware crash types that use the same name (brownout, hardware watchdog
    /// timeout, power cycles, etc.).
    const DEFAULT_PROGRAM_NAME: &'static str = "device";

    /// Handle a FileCrashReport message by sending the specified crash report signature over the
    /// channel to the crash report sender.
    fn handle_file_crash_report(
        &self,
        signature: String,
    ) -> Result<MessageReturn, PowerManagerError> {
        // Try to send the crash report signature over the channel. If the channel is full, return
        // an error
        match self.crash_report_sender.borrow_mut().try_send(signature) {
            Ok(()) => Ok(MessageReturn::FileCrashReport),
            Err(e) if e.is_full() => Err(PowerManagerError::OutOfSpace(format!(
                "Pending crash reports exceeds max ({})",
                MAX_PENDING_CRASH_REPORTS
            ))),
            Err(e) => Err(PowerManagerError::GenericError(e.into())),
        }
    }

    /// Spawn an infinite future that receives crash report signatures over the channel and uses the
    /// proxy to send a File FIDL request to the CrashReporter service with the specified
    /// signatures.
    fn begin_crash_report_sender(
        proxy: fidl_feedback::CrashReporterProxy,
        mut receive_channel: mpsc::Receiver<String>,
    ) {
        fasync::Task::local(async move {
            while let Some(signature) = receive_channel.next().await {
                log_if_err!(
                    Self::send_crash_report(&proxy, signature).await,
                    "Failed to file crash report"
                );
            }
        })
        .detach();
    }

    /// Send a File request to the CrashReporter service with the specified crash report signature.
    async fn send_crash_report(
        proxy: &fidl_feedback::CrashReporterProxy,
        signature: String,
    ) -> Result<(), Error> {
        let report = fidl_feedback::CrashReport {
            program_name: Some(CrashReportHandler::DEFAULT_PROGRAM_NAME.to_string()),
            specific_report: Some(fidl_feedback::SpecificCrashReport::Generic(
                fidl_feedback::GenericCrashReport { crash_signature: Some(signature) },
            )),
            ..fidl_feedback::CrashReport::empty()
        };

        let result = proxy.file(report).await.map_err(|e| format_err!("IPC error: {}", e))?;
        result.map_err(|e| format_err!("Service error: {}", e))
    }
}

#[async_trait(?Send)]
impl Node for CrashReportHandler {
    fn name(&self) -> String {
        "CrashReportHandler".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::FileCrashReport(signature) => {
                self.handle_file_crash_report(signature.to_string())
            }
            _ => Err(PowerManagerError::Unsupported),
        }
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
            .handle_message(&Message::FileCrashReport(crash_report_signature.to_string()))
            .await
            .unwrap();

        // Verify the fake service receives the crash report with expected data
        if let Ok(Some(fidl_feedback::CrashReporterRequest::File { responder: _, report })) =
            stream.try_next().await
        {
            assert_eq!(
                report,
                fidl_feedback::CrashReport {
                    program_name: Some("device".to_string()),
                    specific_report: Some(fidl_feedback::SpecificCrashReport::Generic(
                        fidl_feedback::GenericCrashReport {
                            crash_signature: Some(crash_report_signature.to_string())
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
                    .handle_message(&Message::FileCrashReport("TestCrash1".to_string()))
                    .await,
                Ok(MessageReturn::FileCrashReport)
            );

            // The second FileCrashReport should also succeed because since the first is now in
            // progress, this is now the first "pending" report request
            assert_matches!(
                crash_report_handler
                    .handle_message(&Message::FileCrashReport("TestCrash2".to_string()))
                    .await,
                Ok(MessageReturn::FileCrashReport)
            );

            // Since the first request has not completed, and there is already one pending request,
            // this request should fail
            assert_matches!(
                crash_report_handler
                    .handle_message(&Message::FileCrashReport("TestCrash3".to_string()))
                    .await,
                Err(PowerManagerError::OutOfSpace(_))
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
                        program_name: Some("device".to_string()),
                        specific_report: Some(fidl_feedback::SpecificCrashReport::Generic(
                            fidl_feedback::GenericCrashReport {
                                crash_signature: Some("TestCrash1".to_string())
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
                        program_name: Some("device".to_string()),
                        specific_report: Some(fidl_feedback::SpecificCrashReport::Generic(
                            fidl_feedback::GenericCrashReport {
                                crash_signature: Some("TestCrash2".to_string())
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
