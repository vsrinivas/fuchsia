// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{diagnostics::BatchIteratorConnectionStats, error::AccessorError},
    fidl_fuchsia_diagnostics::{DataType, FormattedContent, StreamMode, MAXIMUM_ENTRIES_PER_BATCH},
    fuchsia_zircon as zx,
    futures::prelude::*,
    serde::Serialize,
    std::{
        io::{BufWriter, Result as IoResult, Write},
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
    tracing::{error, warn},
};

pub type FormattedStream =
    Pin<Box<dyn Stream<Item = Vec<Result<FormattedContent, AccessorError>>> + Send>>;

#[pin_project::pin_project]
pub struct FormattedContentBatcher<C> {
    #[pin]
    items: C,
    stats: Arc<BatchIteratorConnectionStats>,
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
    stats: Arc<BatchIteratorConnectionStats>,
    mode: StreamMode,
) -> FormattedStream
where
    I: Stream<Item = Result<T, E>> + Send + 'static,
    T: Into<FormattedContent> + Send,
    E: Into<AccessorError> + Send,
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
    T: Into<FormattedContent>,
    E: Into<AccessorError>,
{
    type Item = Vec<Result<FormattedContent, AccessorError>>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = self.project();
        match this.items.poll_next(cx) {
            Poll::Ready(Some(chunk)) => {
                // loop over chunk instead of into_iter/map because we can't move `this`
                let mut batch = vec![];
                for item in chunk {
                    let result = match item {
                        Ok(i) => Ok(i.into()),
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

#[derive(Clone)]
struct VmoWriter {
    // TODO(https://fxbug.dev/106877) make this an async lock
    inner: Arc<std::sync::Mutex<InnerVmoWriter>>,
}

enum InnerVmoWriter {
    Active { vmo: zx::Vmo, capacity: u64, tail: u64 },
    Done,
}

impl VmoWriter {
    // TODO(fxbug.dev/48669): take the name of the VMO as well.
    fn new(start_size: u64) -> Self {
        let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, start_size)
            .expect("can always create resizable vmo's");
        let capacity = vmo.get_size().expect("can always read vmo size");
        Self {
            inner: Arc::new(std::sync::Mutex::new(InnerVmoWriter::Active {
                vmo,
                capacity,
                tail: 0,
            })),
        }
    }

    fn tail(&self) -> u64 {
        let guard = self.inner.lock().unwrap();
        match &*guard {
            InnerVmoWriter::Done => 0,
            InnerVmoWriter::Active { tail, .. } => *tail,
        }
    }

    fn capacity(&self) -> u64 {
        let guard = self.inner.lock().unwrap();
        match &*guard {
            InnerVmoWriter::Done => 0,
            InnerVmoWriter::Active { capacity, .. } => *capacity,
        }
    }

    fn finalize(self) -> Option<(zx::Vmo, u64)> {
        let mut inner = self.inner.lock().unwrap();
        let mut swapped = InnerVmoWriter::Done;
        std::mem::swap(&mut *inner, &mut swapped);
        match swapped {
            InnerVmoWriter::Done => None,
            InnerVmoWriter::Active { vmo, tail, .. } => Some((vmo, tail)),
        }
    }

    fn reset(&mut self, new_tail: u64, new_capacity: u64) {
        let mut inner = self.inner.lock().unwrap();
        match &mut *inner {
            InnerVmoWriter::Done => {}
            InnerVmoWriter::Active { vmo, capacity, tail } => {
                vmo.set_size(new_capacity).expect("can always resize a plain vmo");
                *capacity = new_capacity;
                *tail = new_tail;
            }
        }
    }
}

impl Write for VmoWriter {
    fn write(&mut self, buf: &[u8]) -> IoResult<usize> {
        match &mut *self.inner.lock().unwrap() {
            InnerVmoWriter::Done => Ok(0),
            InnerVmoWriter::Active { vmo, tail, capacity } => {
                let new_tail = *tail + buf.len() as u64;
                if new_tail > *capacity {
                    vmo.set_size(new_tail).expect("can always resize a plain vmo");
                    *capacity = new_tail;
                }
                vmo.write(buf, *tail)?;
                *tail = new_tail;
                Ok(buf.len())
            }
        }
    }

    fn flush(&mut self) -> IoResult<()> {
        Ok(())
    }
}

/// Holds a VMO containing valid JSON as well as the size of that json string.
pub struct JsonString {
    pub vmo: zx::Vmo,
    pub size: u64,
}

impl JsonString {
    pub fn serialize(source: &impl Serialize, data_type: DataType) -> Result<Self, AccessorError> {
        let writer = VmoWriter::new(match data_type {
            DataType::Inspect => inspect_format::constants::DEFAULT_VMO_SIZE_BYTES as u64,
            // Logs won't go through this codepath anyway, but in case we ever want to serialize a
            // single log instance it makes sense to start at the page size.
            DataType::Logs => 4096, // page size
        });
        let batch_writer = BufWriter::new(writer.clone());
        serde_json::to_writer(batch_writer, source).map_err(AccessorError::Serialization)?;
        // Safe to unwrap we should always be able to take the vmo here.
        let (vmo, tail) = writer.finalize().unwrap();
        Ok(Self { vmo, size: tail })
    }
}

impl From<JsonString> for FormattedContent {
    fn from(string: JsonString) -> FormattedContent {
        FormattedContent::Json(fidl_fuchsia_mem::Buffer { vmo: string.vmo, size: string.size })
    }
}

/// Wraps an iterator over serializable items and yields FormattedContents, packing items
/// into a JSON array in each VMO up to the size limit provided.
#[pin_project::pin_project]
pub struct JsonPacketSerializer<I, S> {
    #[pin]
    items: I,
    stats: Option<Arc<BatchIteratorConnectionStats>>,
    max_packet_size: u64,
    overflow: Option<S>,
}

impl<I, S> JsonPacketSerializer<I, S> {
    pub fn new(stats: Arc<BatchIteratorConnectionStats>, max_packet_size: u64, items: I) -> Self {
        Self { items, stats: Some(stats), max_packet_size, overflow: None }
    }

    pub fn new_without_stats(max_packet_size: u64, items: I) -> Self {
        Self { items, max_packet_size, overflow: None, stats: None }
    }
}

impl<I, S> Stream for JsonPacketSerializer<I, S>
where
    I: Stream<Item = S> + Unpin,
    S: Serialize,
{
    type Item = Result<JsonString, AccessorError>;

    /// Serialize log messages in a JSON array up to the maximum size provided. Returns Ok(None)
    /// when there are no more messages to serialize.
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut this = self.project();
        let mut writer = VmoWriter::new(*this.max_packet_size);
        writer.write_all(&[b'['])?;

        if let Some(item) = this.overflow.take() {
            let batch_writer = BufWriter::new(writer.clone());
            serde_json::to_writer(batch_writer, &item)?;
            if let Some(stats) = &this.stats {
                stats.add_result();
            }
        }

        let mut items_is_pending = false;
        loop {
            let item = match this.items.poll_next_unpin(cx) {
                Poll::Ready(Some(item)) => item,
                Poll::Ready(None) => break,
                Poll::Pending => {
                    items_is_pending = true;
                    break;
                }
            };

            let writer_tail = writer.tail();
            let is_first = writer_tail == 1;
            let (last_tail, previous_size) = (writer_tail, writer.capacity());
            if !is_first {
                writer.write_all(",\n".as_bytes())?;
            }
            let batch_writer = BufWriter::new(writer.clone());
            serde_json::to_writer(batch_writer, &item)?;
            let writer_tail = writer.tail();
            let item_len = writer_tail - last_tail;

            // +1 for the ending bracket
            if item_len + 1 >= *this.max_packet_size {
                warn!(
                    "serializing oversize item into packet (limit={} actual={})",
                    *this.max_packet_size,
                    writer_tail - last_tail,
                );
            }

            // existing batch + item + array end bracket
            if writer_tail + 1 > *this.max_packet_size {
                writer.reset(last_tail, previous_size);
                *this.overflow = Some(item);
                break;
            }

            if let Some(stats) = &this.stats {
                stats.add_result();
            }
        }

        writer.write_all(&[b']'])?;
        let writer_tail = writer.tail();
        if writer_tail > *this.max_packet_size {
            error!(
                actual = writer_tail,
                max = *this.max_packet_size,
                "returned a string longer than maximum specified",
            )
        }

        // we only want to return an item if we wrote more than opening & closing brackets,
        // and as a string the batch's length is measured in bytes
        if writer_tail > 2 {
            // safe to unwrap, the vmo is guaranteed to be present.
            let (vmo, size) = writer.finalize().unwrap();
            Poll::Ready(Some(Ok(JsonString { vmo, size })))
        } else if items_is_pending {
            Poll::Pending
        } else {
            Poll::Ready(None)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::diagnostics::AccessorStats;
    use futures::stream::iter;

    #[fuchsia::test]
    async fn two_items_joined_and_split() {
        let inputs = &[&"FFFFFFFFFF", &"GGGGGGGGGG"];
        let joined = &["[\"FFFFFFFFFF\",\n\"GGGGGGGGGG\"]"];
        let split = &[r#"["FFFFFFFFFF"]"#, r#"["GGGGGGGGGG"]"#];
        let smallest_possible_joined_len = joined[0].len() as u64;

        let make_packets = |max| async move {
            let node = fuchsia_inspect::Node::default();
            let accessor_stats = Arc::new(AccessorStats::new(node));
            let test_stats = Arc::new(accessor_stats.new_logs_batch_iterator());
            JsonPacketSerializer::new(test_stats, max, iter(inputs.iter()))
                .collect::<Vec<_>>()
                .await
                .into_iter()
                .map(|r| {
                    let result = r.unwrap();
                    let mut buf = vec![0; result.size as usize];
                    result.vmo.read(&mut buf, 0).expect("reading vmo");
                    std::str::from_utf8(&buf).unwrap().to_string()
                })
                .collect::<Vec<_>>()
        };

        let actual_joined = make_packets(smallest_possible_joined_len).await;
        assert_eq!(&actual_joined[..], joined);

        let actual_split = make_packets(smallest_possible_joined_len - 1).await;
        assert_eq!(&actual_split[..], split);
    }
}
