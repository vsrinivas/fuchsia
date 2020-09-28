// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A utility crate for validating the behavior of LogSink & Log implementations.

#![warn(missing_docs)]

use {
    fidl_fuchsia_logger::{
        LogFilterOptions, LogListenerSafeMarker, LogListenerSafeRequest,
        LogListenerSafeRequestStream, LogMessage, LogProxy,
    },
    fuchsia_async as fasync,
    futures::{
        channel::mpsc::{channel, Receiver, Sender},
        sink::SinkExt,
        stream::StreamExt,
    },
};

/// Test that all of the expected message arrive over `proxy`, with no unexpected ones appearing.
/// Returns once all expected messages have been observed.
///
/// # Panics
///
/// Panics when validation fails due to an unexpected message or due to connection failures.
pub async fn validate_log_stream(
    expected: impl IntoIterator<Item = LogMessage>,
    proxy: LogProxy,
    filter_options: Option<LogFilterOptions>,
) {
    ValidatingListener::new(expected).run(proxy, filter_options, false).await;
}

/// Test that all of the expected message arrive over `proxy` after requesting a log dump, with no
/// unexpected records appearing. Returns once all expected messages have been observed.
///
/// # Panics
///
/// Panics when validation fails due to an unexpected message, missing messages when the sink says
/// it is done dumping, or due to connection failures.
pub async fn validate_log_dump(
    expected: impl IntoIterator<Item = LogMessage>,
    proxy: LogProxy,
    filter_options: Option<LogFilterOptions>,
) {
    ValidatingListener::new(expected).run(proxy, filter_options, true).await;
}

enum Outcome {
    AllExpectedReceived,
    LogSentDone,
    UnexpectedMessage(LogMessage),
}

/// Listens to all log messages sent during test, and verifies that they match what's expected.
struct ValidatingListener {
    expected: Vec<LogMessage>,
    outcomes: Option<Receiver<Outcome>>,
    send_outcomes: Sender<Outcome>,
}

impl ValidatingListener {
    fn new(expected: impl IntoIterator<Item = LogMessage>) -> Self {
        let (send_outcomes, outcomes) = channel(3);
        Self { expected: expected.into_iter().collect(), send_outcomes, outcomes: Some(outcomes) }
    }

    /// Drive a LogListenerSafe request stream. Signals for channel close and test completion are
    /// send on the futures-aware channels with which ValidatingListener is constructed.
    async fn run(
        mut self,
        proxy: LogProxy,
        mut filter_options: Option<LogFilterOptions>,
        dump_logs: bool,
    ) {
        let (client_end, stream) =
            fidl::endpoints::create_request_stream::<LogListenerSafeMarker>().unwrap();
        let filter_options = filter_options.as_mut();

        if dump_logs {
            proxy.dump_logs_safe(client_end, filter_options).expect("failed to register listener");
        } else {
            proxy.listen_safe(client_end, filter_options).expect("failed to register listener");
        }

        let mut sink_says_done = false;
        let mut all_expected = false;
        let mut outcomes = self.outcomes.take().unwrap();
        fasync::Task::spawn(self.handle_stream(stream)).detach();

        'observe_outcomes: while let Some(outcome) = outcomes.next().await {
            match outcome {
                Outcome::AllExpectedReceived => all_expected = true,
                Outcome::LogSentDone => sink_says_done = true,
                Outcome::UnexpectedMessage(msg) => panic!("unexpected log message {:?}", msg),
            }

            if all_expected && (!dump_logs || sink_says_done) {
                // only stop looking at outcomes if we have all the messages we expect AND
                // if we either don't care about log dumps terminating because we didn't ask for one
                // or it has terminated as we expect
                break 'observe_outcomes;
            }
        }

        if dump_logs {
            assert!(sink_says_done, "must have received all expected messages");
        } else {
            // FIXME(41966): this should be tested for both streaming and dumping modes
            assert!(all_expected, "must have received all expected messages");
        }
    }

    async fn handle_stream(mut self, mut stream: LogListenerSafeRequestStream) {
        while let Some(Ok(req)) = stream.next().await {
            self.handle_request(req).await;
        }
    }

    async fn handle_request(&mut self, req: LogListenerSafeRequest) {
        match req {
            LogListenerSafeRequest::Log { log, responder } => {
                self.log(log).await;
                responder.send().ok();
            }
            LogListenerSafeRequest::LogMany { log, responder } => {
                for msg in log {
                    self.log(msg).await;
                }
                responder.send().ok();
            }
            LogListenerSafeRequest::Done { .. } => {
                self.send_outcomes.send(Outcome::LogSentDone).await.unwrap();
            }
        }
    }

    async fn log(&mut self, received: LogMessage) {
        if let Some((i, _)) = self.expected.iter().enumerate().find(|(_, expected)| {
            expected.msg == received.msg
                && expected.tags == received.tags
                && expected.severity == received.severity
        }) {
            self.expected.remove(i);
            if self.expected.is_empty() {
                self.send_outcomes.send(Outcome::AllExpectedReceived).await.unwrap();
            }
        } else {
            self.send_outcomes.send(Outcome::UnexpectedMessage(received)).await.unwrap();
        }
    }
}
