// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    diagnostics_reader::{ArchiveReader, Inspect},
    tracing::*,
};

// Selectors for Inspect data must start with this exact string.
const INSPECT_PREFIX: &str = "INSPECT:";

/// `InspectFetcher` fetches data from a list of selectors from FeedbackArchiveAccessor.
pub struct InspectFetcher {
    // If we have no selectors, we don't want to actually fetch anything.
    // (Fetching with no selectors fetches all Inspect data.)
    reader: Option<ArchiveReader>,
}

impl std::fmt::Debug for InspectFetcher {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("InspectFetcher").field("reader", &"opaque-ArchiveReader").finish()
    }
}

impl InspectFetcher {
    /// Creates an InspectFetcher or returns an error. Note: If no selectors are given,
    /// fetch() will return "[]" instead of fetching all Inspect data.
    ///
    /// `service_path` should name a fuchsia.diagnostics.ArchiveAccessor service.
    /// `selectors` should be in Triage format, i.e. INSPECT:moniker:path:leaf.

    pub fn create(service_path: &str, selectors: Vec<String>) -> Result<InspectFetcher, Error> {
        if selectors.len() == 0 {
            return Ok(InspectFetcher { reader: None });
        }
        let proxy = match fuchsia_component::client::connect_to_protocol_at_path::<
            fidl_fuchsia_diagnostics::ArchiveAccessorMarker,
        >(service_path)
        {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Inspect reader: {}", e),
        };
        let mut reader = ArchiveReader::new();
        reader
            .with_archive(proxy)
            .retry_if_empty(false)
            .add_selectors(Self::process_selectors(selectors)?.into_iter());
        Ok(InspectFetcher { reader: Some(reader) })
    }

    /// Fetches the selectee Inspect data.
    /// Data is returned as a String in JSON format because that's what TriageLib needs.
    pub async fn fetch(&mut self) -> Result<String, Error> {
        match &self.reader {
            None => Ok("[]".to_string()),
            Some(reader) => {
                // TODO(fxbug.dev/62480): Make TriageLib accept structured data
                Ok(reader.snapshot_raw::<Inspect>().await?.to_string())
            }
        }
    }

    fn process_selectors(selectors: Vec<String>) -> Result<Vec<String>, Error> {
        let get_inspect = |s: String| -> Option<std::string::String> {
            if &s[..INSPECT_PREFIX.len()] == INSPECT_PREFIX {
                Some(s[INSPECT_PREFIX.len()..].to_string())
            } else {
                warn!("All selectors should begin with 'INSPECT:' - '{}'", s);
                None
            }
        };
        Ok(selectors.into_iter().filter_map(get_inspect).collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    async fn test_selector_acceptance() {
        let empty_vec = vec![];
        let ok_selectors =
            vec!["INSPECT:moniker:path:leaf".to_string(), "INSPECT:name:nodes:item".to_string()];
        let ok_processed = vec!["moniker:path:leaf".to_string(), "name:nodes:item".to_string()];

        let bad_selector = vec![
            "INSPECT:moniker:path:leaf".to_string(),
            "FOO:moniker:path:leaf".to_string(),
            "INSPECT:name:nodes:item".to_string(),
        ];

        assert_eq!(InspectFetcher::process_selectors(empty_vec).unwrap(), Vec::<String>::new());
        assert_eq!(InspectFetcher::process_selectors(ok_selectors).unwrap(), ok_processed);
        assert_eq!(InspectFetcher::process_selectors(bad_selector).unwrap(), ok_processed);
    }
}
