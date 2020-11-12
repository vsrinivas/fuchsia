// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{diagnostics::DiagnosticsServerStats, server::ServerError},
    fidl_fuchsia_diagnostics::{FormattedContent, StreamMode, MAXIMUM_ENTRIES_PER_BATCH},
    fuchsia_zircon as zx,
    futures::prelude::*,
    log::{error, warn},
    serde::Serialize,
    std::{
        convert::TryInto,
        ops::Deref,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
};

pub type FormattedStream =
    Pin<Box<dyn Stream<Item = Vec<Result<FormattedContent, ServerError>>> + Send>>;

#[pin_project::pin_project]
pub struct FormattedContentBatcher<C> {
    #[pin]
    items: C,
    stats: Arc<DiagnosticsServerStats>,
}

/// Make a new `FormattedContentBatcher` with a chunking strategy depending on stream mode.
///
/// In snapshot mode, batched items will not be flushed to the client until the batch is complete
/// or the underlying stream has terminated.
///
/// In subscribe or snapshot-then-subscribe mode, batched items will be flushed whenever the
/// underlying stream is pending, ensuring clients always receive latest results.
pub fn new_batcher<I, T, E>(
    items: I,
    stats: Arc<DiagnosticsServerStats>,
    mode: StreamMode,
) -> FormattedStream
where
    I: Stream<Item = Result<T, E>> + Send + 'static,
    T: TryInto<FormattedContent, Error = ServerError> + Send,
    E: Into<ServerError> + Send,
{
    match mode {
        StreamMode::Subscribe | StreamMode::SnapshotThenSubscribe => {
            Box::pin(FormattedContentBatcher {
                items: items.ready_chunks(MAXIMUM_ENTRIES_PER_BATCH as _),
                stats,
            })
        }
        StreamMode::Snapshot => Box::pin(FormattedContentBatcher {
            items: items.chunks(MAXIMUM_ENTRIES_PER_BATCH as _),
            stats,
        }),
    }
}

impl<I, T, E> Stream for FormattedContentBatcher<I>
where
    I: Stream<Item = Vec<Result<T, E>>>,
    T: TryInto<FormattedContent, Error = ServerError>,
    E: Into<ServerError>,
{
    type Item = Vec<Result<FormattedContent, ServerError>>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = self.project();
        match this.items.poll_next(cx) {
            Poll::Ready(Some(chunk)) => {
                // loop over chunk instead of into_iter/map because we can't move `this`
                let mut batch = vec![];
                for item in chunk {
                    let result = match item {
                        Ok(i) => i.try_into(),
                        Err(e) => {
                            this.stats.add_result_error();
                            Err(e.into())
                        }
                    };
                    batch.push(result);
                }
                Poll::Ready(Some(batch))
            }
            Poll::Ready(None) => Poll::Ready(None),
            Poll::Pending => Poll::Pending,
        }
    }
}

/// A string whose contents are valid JSON.
pub struct JsonString(String);

impl JsonString {
    pub fn serialize(source: &impl Serialize) -> Result<Self, serde_json::Error> {
        serde_json::to_string_pretty(source).map(JsonString)
    }
}

impl TryInto<FormattedContent> for JsonString {
    type Error = ServerError;

    fn try_into(self) -> Result<FormattedContent, Self::Error> {
        let size = self.len() as u64;
        let vmo = zx::Vmo::create(size).map_err(ServerError::VmoCreate)?;
        vmo.write(self.as_bytes(), 0).map_err(ServerError::VmoWrite)?;
        Ok(FormattedContent::Json(fidl_fuchsia_mem::Buffer { vmo, size }))
    }
}

impl Deref for JsonString {
    type Target = str;
    fn deref(&self) -> &Self::Target {
        &*self.0
    }
}

/// Wraps an iterator over serializable items and yields FormattedContents, packing items
/// into a JSON array in each VMO up to the size limit provided.
pub struct JsonPacketSerializer<I> {
    items: I,
    stats: Arc<DiagnosticsServerStats>,
    max_packet_size: usize,
    overflow: Option<String>,
}

impl<I> JsonPacketSerializer<I> {
    pub fn new(stats: Arc<DiagnosticsServerStats>, max_packet_size: usize, items: I) -> Self {
        Self { items, stats, max_packet_size, overflow: None }
    }
}

impl<I, S> Stream for JsonPacketSerializer<I>
where
    I: Stream<Item = S> + Unpin,
    S: Serialize,
{
    type Item = Result<JsonString, serde_json::Error>;

    /// Serialize log messages in a JSON array up to the maximum size provided. Returns Ok(None)
    /// when there are no more messages to serialize.
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut batch = String::from("[");

        if let Some(item) = self.overflow.take() {
            batch.push_str(&item);
            self.stats.add_result();
        }

        let mut items_is_pending = false;
        loop {
            let item = match self.items.poll_next_unpin(cx) {
                Poll::Ready(Some(item)) => item,
                Poll::Ready(None) => break,
                Poll::Pending => {
                    items_is_pending = true;
                    break;
                }
            };

            let item = serde_json::to_string(&item)?;
            if item.len() >= self.max_packet_size {
                warn!(
                    "serializing oversize item into packet (limit={} actual={})",
                    self.max_packet_size,
                    item.len()
                );
            }

            let is_first = batch.len() == 1;
            // items after the first will have a comma *and* newline *and* ending array bracket
            let pending_len = item.len() + if is_first { 1 } else { 3 };

            // existing batch + item + array end bracket
            if batch.len() + pending_len > self.max_packet_size {
                self.overflow = Some(item);
                break;
            }

            if !is_first {
                batch.push_str(",\n");
            }
            batch.push_str(&item);
            self.stats.add_result();
        }

        batch.push_str("]");
        if batch.len() > self.max_packet_size {
            error!(
                "returned a string longer than maximum specified (actual {}, max {})",
                batch.len(),
                self.max_packet_size
            )
        }

        // we only want to return an item if we wrote more than opening & closing brackets,
        // and as a string the batch's length is measured in bytes
        if batch.len() > 2 {
            Poll::Ready(Some(Ok(JsonString(batch))))
        } else {
            if items_is_pending {
                Poll::Pending
            } else {
                Poll::Ready(None)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::diagnostics::ArchiveAccessorStats;
    use futures::stream::iter;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn two_items_joined_and_split() {
        let inputs = &[&"FFFFFFFFFF", &"GGGGGGGGGG"];
        let joined = &["[\"FFFFFFFFFF\",\n\"GGGGGGGGGG\"]"];
        let split = &[r#"["FFFFFFFFFF"]"#, r#"["GGGGGGGGGG"]"#];
        let smallest_possible_joined_len = joined[0].len();

        let make_packets = |max| async move {
            let node = fuchsia_inspect::Node::default();
            let accessor_stats = Arc::new(ArchiveAccessorStats::new(node));
            let test_stats = Arc::new(DiagnosticsServerStats::for_logs(accessor_stats));
            JsonPacketSerializer::new(test_stats, max, iter(inputs.iter()))
                .collect::<Vec<_>>()
                .await
                .into_iter()
                .map(|s| s.unwrap().0)
                .collect::<Vec<_>>()
        };

        let actual_joined = make_packets(smallest_possible_joined_len).await;
        assert_eq!(&actual_joined[..], joined);

        let actual_split = make_packets(smallest_possible_joined_len - 1).await;
        assert_eq!(&actual_split[..], split);
    }
}
