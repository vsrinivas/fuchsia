// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(cphoenix): This code is copied from Detect:diagnostics.rs. Worth splitting
// out into a library?

use {
    anyhow::{bail, Error},
    diagnostics_reader::{ArchiveReader, Inspect},
    log::*,
};

// Selectors for Inspect data must start with this exact string.
const INSPECT_PREFIX: &str = "INSPECT:";
// The capability name for the Inspect reader
const INSPECT_SERVICE_PATH: &str = "/svc/fuchsia.diagnostics.FeedbackArchiveAccessor";

pub struct InspectFetcher {
    reader: ArchiveReader,
}

impl std::fmt::Debug for InspectFetcher {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("InspectFetcher").field("reader", &"opaque-ArchiveReader").finish()
    }
}

impl InspectFetcher {
    pub fn create(selectors: Vec<String>) -> Result<InspectFetcher, Error> {
        if selectors.len() == 0 {
            // If we have no selectors, we don't want to actually fetch anything.
            // (Fetching with no selectors fetches all Inspect data.)
            bail!("At least one selector is required");
        }
        let proxy = match fuchsia_component::client::connect_to_service_at_path::<
            fidl_fuchsia_diagnostics::ArchiveAccessorMarker,
        >(INSPECT_SERVICE_PATH)
        {
            Ok(proxy) => proxy,
            Err(e) => bail!("Failed to connect to Inspect reader: {}", e),
        };
        let reader = ArchiveReader::new().with_archive(proxy);
        let get_inspect = |s: String| -> Option<std::string::String> {
            if &s[..INSPECT_PREFIX.len()] == INSPECT_PREFIX {
                Some(s[INSPECT_PREFIX.len()..].to_string())
            } else {
                error!("All selectors should begin with 'INSPECT:' - '{}'", s);
                None
            }
        };
        let selectors = selectors.into_iter().filter_map(get_inspect);
        let reader = reader.retry_if_empty(false).add_selectors(selectors);
        Ok(InspectFetcher { reader })
    }

    /// This returns a String in JSON format because that's what TriageLib needs.
    pub async fn fetch(&mut self) -> Result<String, Error> {
        Ok(self.reader.snapshot_raw::<Inspect>().await?.to_string())
    }
}
