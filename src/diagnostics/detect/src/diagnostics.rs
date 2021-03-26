// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fetches diagnostic data.

use {anyhow::Error, inspect_fetcher::InspectFetcher, triage::DiagnosticData, triage::Source};

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
        Ok(DiagnosticFetcher {
            inspect: InspectFetcher::create(INSPECT_SERVICE_PATH, selectors.inspect_selectors)?,
        })
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
