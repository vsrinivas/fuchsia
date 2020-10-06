// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{diagnostics::DiagnosticsServerStats, server::ServerError},
    anyhow::Context as _,
    fidl_fuchsia_diagnostics::{FormattedContent, MAXIMUM_ENTRIES_PER_BATCH},
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

/// Serialize the `contents` to JSON in a VMO, returned as a `FormattedContent`.
pub fn serialize_to_formatted_json_content(
    contents: impl Serialize,
) -> Result<FormattedContent, anyhow::Error> {
    let content_string = serde_json::to_string_pretty(&contents)?;
    make_json_formatted_content(&content_string)
}

/// Produces a `FormattedContent` with the provided JSON string as its contents. Does not validate
/// that `content_string` is JSON.
fn make_json_formatted_content(content_string: &str) -> Result<FormattedContent, anyhow::Error> {
    let size = content_string.len() as u64;
    let vmo = zx::Vmo::create(size).context("error creating buffer")?;
    vmo.write(content_string.as_bytes(), 0).context("error writing buffer")?;
    Ok(FormattedContent::Json(fidl_fuchsia_mem::Buffer { vmo, size }))
}

#[pin_project::pin_project]
pub struct FormattedContentBatcher<C> {
    #[pin]
    items: C,
    stats: Arc<DiagnosticsServerStats>,
}

impl<I, E> FormattedContentBatcher<futures::stream::ReadyChunks<I>>
where
    I: Stream<Item = Result<JsonString, E>>,
    E: Into<ServerError>,
{
    pub fn new(items: I, stats: Arc<DiagnosticsServerStats>) -> Self {
        Self { items: items.ready_chunks(MAXIMUM_ENTRIES_PER_BATCH as _), stats }
    }
}

impl<I, T, E> Stream for FormattedContentBatcher<futures::stream::ReadyChunks<I>>
where
    I: Stream<Item = Result<T, E>>,
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

impl<I, P, S> Stream for JsonPacketSerializer<I>
where
    I: Stream<Item = P> + Unpin,
    P: Deref<Target = S>,
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

            let item = serde_json::to_string(&*item)?;
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
