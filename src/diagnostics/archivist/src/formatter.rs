// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::diagnostics::DiagnosticsServerStats,
    anyhow::{ensure, Context as _, Error},
    fidl_fuchsia_diagnostics::{FormattedContent, MAXIMUM_ENTRIES_PER_BATCH},
    fuchsia_zircon as zx,
    serde::Serialize,
    std::sync::Arc,
};

/// Serialize the `contents` to JSON in a VMO, returned as a `FormattedContent`.
pub fn serialize_to_formatted_json_content(
    contents: impl Serialize,
) -> Result<FormattedContent, Error> {
    let content_string = serde_json::to_string_pretty(&contents)?;
    make_json_formatted_content(&content_string)
}

/// Produces a `FormattedContent` with the provided JSON string as its contents. Does not validate
/// that `content_string` is JSON.
fn make_json_formatted_content(content_string: &str) -> Result<FormattedContent, Error> {
    let size = content_string.len() as u64;
    let vmo = zx::Vmo::create(size).context("error creating buffer")?;
    vmo.write(content_string.as_bytes(), 0).context("error writing buffer")?;
    Ok(FormattedContent::Json(fidl_fuchsia_mem::Buffer { vmo, size }))
}

/// Wraps an iterator over serializable items and yields FormattedContents, packing items
/// into a JSON array in each VMO up to the size limit provided.
pub struct ChunkedJsonArraySerializer<I> {
    items: I,
    stats: Arc<DiagnosticsServerStats>,
    max_packet_size: usize,
    overflow: Option<String>,
}

impl<I, S> ChunkedJsonArraySerializer<I>
where
    I: Iterator<Item = S>,
    S: Serialize,
{
    /// Construct a new chunked JSON serializer.
    ///
    /// # Caveats
    ///
    /// Use caution when specifying `max_packet_size` here, as any message which is individually
    /// larger than the maximum will cause an error. In production this value should always be
    /// populated from very large constants.
    pub fn new(stats: Arc<DiagnosticsServerStats>, max_packet_size: usize, items: I) -> Self {
        Self { items, stats, max_packet_size, overflow: None }
    }

    /// Produce the next batch of `FormattedContent`s.
    pub fn next_batch(&mut self) -> Result<Vec<FormattedContent>, Error> {
        let mut batch = Vec::new();

        while batch.len() < MAXIMUM_ENTRIES_PER_BATCH as _ {
            match self.pack_items() {
                Ok(Some(item)) => {
                    batch.push(make_json_formatted_content(&item)?);
                }
                Err(_) => {
                    // TODO(fxbug.dev/59660) clarify error semantics and reporting
                    self.stats.add_result_error();
                }
                Ok(None) => break,
            }
        }

        Ok(batch)
    }

    /// Serialize log messages in a JSON array up to the maximum size provided. Returns Ok(None)
    /// when there are no more messages to serialize.
    fn pack_items(&mut self) -> Result<Option<String>, Error> {
        let mut batch = String::from("[");

        if let Some(item) = self.overflow.take() {
            batch.push_str(&item);
            self.stats.add_result();
        }

        for item in &mut self.items {
            let item = serde_json::to_string_pretty(&item)?;
            ensure!(
                item.len() < self.max_packet_size - 1,
                "serialized messages must fit within maximum packet size"
            );

            let is_first = batch.len() == 1;
            // items after the first will have a comma *and* ending array bracket
            let pending_len = item.len() + if is_first { 1 } else { 2 };

            // existing batch + item + array end bracket
            if batch.len() + pending_len > self.max_packet_size {
                self.overflow = Some(item);
                break;
            }

            if !is_first {
                batch.push_str(",");
            }
            batch.push_str(&item);
            self.stats.add_result();
        }

        batch.push_str("]");
        if batch.len() >= self.max_packet_size {
            log::error!(
                "returned a string longer than maximum specified (actual {}, max {})",
                batch.len(),
                self.max_packet_size
            )
        }

        Ok(if batch.len() > 2 {
            Some(batch)
        } else {
            // only brackets were in the string so we didn't write anything
            None
        })
    }
}

impl<I, S> Iterator for ChunkedJsonArraySerializer<I>
where
    I: Iterator<Item = S>,
    S: Serialize,
{
    type Item = Result<String, Error>;
    fn next(&mut self) -> Option<Self::Item> {
        self.pack_items().transpose()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::diagnostics::ArchiveAccessorStats;

    #[test]
    fn two_items_joined_and_split() {
        let inputs = &["FFFFFFFFFF", "GGGGGGGGGG"];
        let joined = &[r#"["FFFFFFFFFF","GGGGGGGGGG"]"#];
        let split = &[r#"["FFFFFFFFFF"]"#, r#"["GGGGGGGGGG"]"#];
        let smallest_possible_joined_len = joined[0].len();

        let make_packets = |max| {
            let node = fuchsia_inspect::Node::default();
            let accessor_stats = Arc::new(ArchiveAccessorStats::new(node));
            let test_stats = Arc::new(DiagnosticsServerStats::for_logs(accessor_stats));
            ChunkedJsonArraySerializer::new(test_stats, max, inputs.iter())
                .collect::<Result<Vec<_>, _>>()
                .unwrap()
        };

        let actual_joined = make_packets(smallest_possible_joined_len);
        assert_eq!(&actual_joined[..], joined);

        let actual_split = make_packets(smallest_possible_joined_len - 1);
        assert_eq!(&actual_split[..], split);
    }
}
