// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fetches diagnostic data.

use {
    anyhow::{bail, Error},
    fuchsia_inspect_contrib::reader::{ArchiveReader, DataType},
    log::error,
    triage::DiagnosticData,
    triage::Source,
};

// Selectors for Inspect data must start with this exact string.
const INSPECT_PREFIX: &str = "INSPECT:";
// The capability name for the Inspect reader
const INSPECT_SERVICE_PATH: &str = "/svc/fuchsia.diagnostics.FeedbackArchiveAccessor";

// Durable connection to Archivist
#[derive(Debug)]
pub struct DiagnosticFetcher {
    inspect: InspectFetcher,
}

#[derive(Debug)]
pub struct Selectors {
    pub(crate) inspect_selectors: Vec<String>,
}

impl DiagnosticFetcher {
    pub fn create(selectors: Selectors) -> Result<DiagnosticFetcher, Error> {
        Ok(DiagnosticFetcher { inspect: InspectFetcher::create(selectors.inspect_selectors)? })
    }

    pub async fn get_diagnostics(&mut self) -> Result<Vec<DiagnosticData>, Error> {
        let inspect_data = DiagnosticData::new(
            "inspect.json".to_string(),
            Source::Inspect,
            self.inspect.fetch().await?,
        )?;
        Ok(vec![inspect_data])
    }
}

impl Selectors {
    pub fn new() -> Selectors {
        Selectors { inspect_selectors: Vec::new() }
    }

    pub fn with_inspect_selectors(mut self, selectors: Vec<String>) -> Self {
        self.inspect_selectors.extend(selectors);
        self
    }
}

struct InspectFetcher {
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
    pub fn create(selectors: Vec<String>) -> Result<InspectFetcher, Error> {
        if selectors.len() == 0 {
            return Ok(InspectFetcher { reader: None });
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
        Ok(InspectFetcher { reader: Some(reader) })
    }

    /// This returns a String in JSON format because that's what TriageLib needs.
    pub async fn fetch(&mut self) -> Result<String, Error> {
        match &self.reader {
            None => Ok("[]".to_string()),
            Some(reader) => {
                // TODO(fxbug.dev/62480): Make TriageLib accept structured data
                Ok(reader.snapshot_raw(DataType::Inspect).await?.to_string())
            }
        }
    }
}
