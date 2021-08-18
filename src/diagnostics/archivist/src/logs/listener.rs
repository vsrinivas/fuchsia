// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::message::Message;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogListenerSafeMarker, LogListenerSafeProxy, LogMessage,
};
use fuchsia_async::Task;
use futures::prelude::*;
use logmessage_measure_tape::measure;
use std::{sync::Arc, task::Poll};
use thiserror::Error;
use tracing::{debug, error, trace};

mod filter;

use filter::MessageFilter;

// Number of bytes the header of a vector occupies in a fidl message.
const FIDL_VECTOR_HEADER_BYTES: usize = 16;

/// An individual log listener. Wraps the FIDL type `LogListenerProxy` in filtering options provided
/// when connecting.
pub struct Listener {
    listener: LogListenerSafeProxy,
    filter: MessageFilter,
    status: Status,
}

#[derive(PartialEq)]
enum Status {
    Fine,
    Stale,
}

impl Listener {
    /// Create a new `Listener`. Fails if `client` can't be converted into a `LogListenerProxy` or
    /// if `LogFilterOptions` are invalid.
    pub fn new(
        log_listener: ClientEnd<LogListenerSafeMarker>,
        options: Option<Box<LogFilterOptions>>,
    ) -> Result<Self, ListenerError> {
        debug!("New listener with options {:?}", &options);
        Ok(Self {
            status: Status::Fine,
            listener: log_listener
                .into_proxy()
                .map_err(|source| ListenerError::CreatingListenerProxy { source })?,
            filter: MessageFilter::new(options)?,
        })
    }

    pub fn spawn(
        self,
        logs: impl Stream<Item = Arc<Message>> + Send + Unpin + 'static,
        call_done: bool,
    ) -> Task<()> {
        Task::spawn(async move { self.run(logs, call_done).await })
    }

    /// Send messages to the listener. First eagerly collects any backlog and sends it out in
    /// batches before waiting for wakeups.
    async fn run(mut self, mut logs: impl Stream<Item = Arc<Message>> + Unpin, call_done: bool) {
        debug!("Backfilling from cursor until pending.");
        let mut backlog = vec![];
        futures::future::poll_fn(|cx| {
            loop {
                match logs.poll_next_unpin(cx) {
                    Poll::Ready(Some(next)) => backlog.push(next),
                    _ => break,
                }
            }

            Poll::Ready(())
        })
        .await;

        self.backfill(backlog).await;
        debug!("Done backfilling.");

        pin_utils::pin_mut!(logs);
        while let Some(message) = logs.next().await {
            self.send_log(&message).await;
        }

        if call_done {
            self.listener.done().ok();
        }
        debug!("Listener exiting.");
    }

    /// Returns whether this listener should continue receiving messages.
    fn is_healthy(&self) -> bool {
        self.status == Status::Fine
    }

    /// Send all messages currently in the provided buffer to this listener. Attempts to batch up
    /// to the message size limit. Returns early if the listener appears to be unhealthy.
    async fn backfill<'a>(&mut self, mut messages: Vec<Arc<Message>>) {
        messages.sort_by_key(|m| m.metadata.timestamp);

        // Initialize batch size to the size of the vector header.
        let mut batch_size = FIDL_VECTOR_HEADER_BYTES;
        let mut filtered_batch = vec![];
        for msg in messages {
            if self.filter.should_send(&msg) {
                // Convert archivist-encoded log message to legacy format expected
                // by the listener, then use measure_tape to get true size.
                let legacy_msg = msg.for_listener();
                let msg_size = measure(&legacy_msg).num_bytes;

                // If a message by itself is too big to fit into fidl, warn and skip.
                if msg_size + FIDL_VECTOR_HEADER_BYTES
                    > fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize
                {
                    trace!("Unable to encode message, it exceeded our MAX_LOG_MANY_SIZE_BYTES by itself.");
                    continue;
                }

                if batch_size + msg_size > fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize {
                    self.send_filtered_logs(&mut filtered_batch).await;
                    if !self.is_healthy() {
                        return;
                    }
                    filtered_batch.clear();
                    batch_size = FIDL_VECTOR_HEADER_BYTES;
                }

                batch_size += msg_size;
                trace!("Batching {:?}.", msg.id);
                filtered_batch.push(legacy_msg);
            }
        }

        if !filtered_batch.is_empty() {
            self.send_filtered_logs(&mut filtered_batch).await;
        }
    }

    /// Send a batch of pre-filtered log messages to this listener.
    async fn send_filtered_logs(&mut self, log_messages: &mut Vec<LogMessage>) {
        trace!("Flushing batch.");
        self.check_result({
            let mut log_messages = log_messages.iter_mut();
            let fut = self.listener.log_many(&mut log_messages);
            fut.await
        });
    }

    /// Send a single log message if it should be sent according to this listener's filter settings.
    async fn send_log(&mut self, log_message: &Message) {
        if self.filter.should_send(log_message) {
            trace!("Sending {:?}.", log_message.id);
            let mut to_send = log_message.for_listener();
            self.check_result(self.listener.log(&mut to_send).await);
        }
    }

    /// Consume the result of sending logs to this listener, potentially marking it stale.
    fn check_result(&mut self, result: Result<(), fidl::Error>) {
        if let Err(e) = result {
            if e.is_closed() {
                self.status = Status::Stale;
            } else {
                error!(?e, "Error calling listener");
            }
        }
    }
}

#[derive(Debug, Error)]
pub enum ListenerError {
    #[error("{count} tags provided, max {}", fidl_fuchsia_logger::MAX_TAGS)]
    TooManyTags { count: usize },

    #[error("tag at index {index} is too long, max {}", fidl_fuchsia_logger::MAX_TAG_LEN_BYTES)]
    TagTooLong { index: usize },

    #[error("couldn't create LogListenerProxy")]
    CreatingListenerProxy { source: fidl::Error },

    #[error("couldn't decode value: {source}")]
    Decode {
        #[from]
        source: super::error::StreamError,
    },

    #[error("error while forwarding unsafe log requests: {source}")]
    AsbestosIo { source: fidl::Error },
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        container::ComponentIdentity,
        events::types::ComponentIdentifier,
        logs::message::{fx_log_packet_t, METADATA_SIZE},
    };

    use fidl::endpoints::ServerEnd;
    use fidl_fuchsia_logger::LogLevelFilter;
    use fidl_fuchsia_logger::LogListenerSafeRequest;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use libc::c_char;
    #[fuchsia::test]
    async fn normal_behavior_test() {
        let message_vec =
            provide_messages(fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize, 4);

        assert_eq!(run_and_consume_backfill(message_vec).await, 4);
    }

    #[fuchsia::test]
    async fn packet_fits_but_converted_struct_would_cause_overflow_test() {
        let message_vec =
            provide_messages(fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize, 1);

        assert_eq!(run_and_consume_backfill(message_vec).await, 0);
    }

    #[fuchsia::test]
    async fn one_packet_would_overflow_but_others_fit_test() {
        let mut message_vec =
            provide_messages(fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize, 1);

        message_vec.append(&mut provide_messages(
            fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize,
            4,
        ));

        assert_eq!(run_and_consume_backfill(message_vec).await, 4);
    }

    async fn run_and_consume_backfill(message_vec: Vec<Arc<Message>>) -> usize {
        let (client, server) = zx::Channel::create().unwrap();
        let client_end = ClientEnd::<LogListenerSafeMarker>::new(client);
        let mut listener_server =
            ServerEnd::<LogListenerSafeMarker>::new(server).into_stream().unwrap();
        let mut listener = Listener::new(client_end, None).unwrap();

        fasync::Task::spawn(async move {
            listener.backfill(message_vec).await;
        })
        .detach();

        let mut observed_logs: usize = 0;
        while let Some(req) = listener_server.try_next().await.unwrap() {
            match req {
                LogListenerSafeRequest::LogMany { log, responder } => {
                    observed_logs += log.len();
                    responder.send().unwrap();
                }
                _ => panic!("only testing backfill mode."),
            }
        }

        observed_logs
    }

    fn provide_messages(summed_msg_size_bytes: usize, num_messages: usize) -> Vec<Arc<Message>> {
        let per_msg_size = summed_msg_size_bytes / num_messages;
        let mut message_vec = Vec::new();
        for _ in 0..num_messages {
            let byte_encoding = generate_byte_encoded_log(per_msg_size);
            message_vec.push(Arc::new(
                Message::from_logger(&get_test_identity(), byte_encoding.as_bytes()).unwrap(),
            ))
        }

        message_vec
    }

    // Generate an fx log packet of a target size with size split between tags and data.
    fn generate_byte_encoded_log(target_size: usize) -> fx_log_packet_t {
        let mut test_packet = test_packet();
        let data_size = target_size - METADATA_SIZE;
        let tag_size =
            core::cmp::min(data_size / 2, fidl_fuchsia_logger::MAX_TAG_LEN_BYTES as usize);
        let message_size = data_size - tag_size;

        populate_packet(&mut test_packet, tag_size, message_size);
        test_packet
    }

    fn test_packet() -> fx_log_packet_t {
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.pid = 1;
        packet.metadata.tid = 2;
        packet.metadata.time = 3;
        packet.metadata.severity = LogLevelFilter::Debug as i32;
        packet.metadata.dropped_logs = 10;
        packet
    }

    fn populate_packet(packet: &mut fx_log_packet_t, tag_count: usize, message_size: usize) {
        let tag_start = 1;
        let tag_end = tag_start + tag_count;

        packet.data[0] = tag_count as c_char;
        packet.fill_data(tag_start..tag_end, 'T' as _);
        packet.data[tag_end] = 0; // terminate tags

        let message_start = tag_start + tag_count + 1;
        let message_end = message_start + message_size;
        packet.fill_data(message_start..message_end, 'D' as _);
    }

    fn get_test_identity() -> ComponentIdentity {
        ComponentIdentity::from_identifier_and_url(
            &ComponentIdentifier::Legacy {
                moniker: vec!["fake-test-env", "bleebloo.cmx"].into(),
                instance_id: "".into(),
            },
            "fuchsia-pkg://fuchsia.com/testing123#test-component.cmx",
        )
    }
}
