// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    diagnostics_data::{LogsData, LogsField, Severity, Timestamp},
    diagnostics_hierarchy::hierarchy,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorRequest,
    },
    futures::TryStreamExt,
    std::sync::Arc,
};

pub struct FakeArchiveIteratorResponse {
    // Note that these are _all_ mutually exclusive.
    values: Vec<String>,
    iterator_error: Option<ArchiveIteratorError>,
    should_fidl_error: bool,
}

impl FakeArchiveIteratorResponse {
    pub fn new_with_values(values: Vec<String>) -> Self {
        Self { values, iterator_error: None, should_fidl_error: false }
    }

    pub fn new_with_error(err: ArchiveIteratorError) -> Self {
        Self { values: vec![], iterator_error: Some(err), should_fidl_error: false }
    }

    pub fn new_with_fidl_error() -> Self {
        Self { values: vec![], iterator_error: None, should_fidl_error: true }
    }
}

pub fn setup_fake_archive_iterator(
    server_end: ServerEnd<ArchiveIteratorMarker>,
    responses: Arc<Vec<FakeArchiveIteratorResponse>>,
) -> Result<()> {
    let mut stream = server_end.into_stream()?;
    fuchsia_async::Task::local(async move {
        let mut iter = responses.iter();
        while let Ok(Some(req)) = stream.try_next().await {
            match req {
                ArchiveIteratorRequest::GetNext { responder } => {
                    let next = iter.next();
                    match next {
                        Some(FakeArchiveIteratorResponse {
                            values,
                            iterator_error,
                            should_fidl_error,
                        }) => {
                            if let Some(err) = iterator_error {
                                responder.send(&mut Err(*err)).unwrap();
                            } else if *should_fidl_error {
                                responder.control_handle().shutdown();
                            } else {
                                responder
                                    .send(&mut Ok(values
                                        .iter()
                                        .map(|s| ArchiveIteratorEntry {
                                            data: Some(s.clone()),
                                            truncated_chars: Some(0),
                                            ..ArchiveIteratorEntry::EMPTY
                                        })
                                        .collect()))
                                    .unwrap()
                            }
                        }
                        None => responder.control_handle().shutdown(),
                    }
                }
            }
        }
    })
    .detach();
    Ok(())
}

pub struct LogsDataBuilder {
    message: String,
    timestamp: Timestamp,
    moniker: String,
    component_url: String,
    severity: Severity,
}

impl LogsDataBuilder {
    pub fn new() -> Self {
        Self {
            message: String::default(),
            timestamp: Timestamp::from(0i64),
            moniker: String::default(),
            component_url: String::default(),
            severity: Severity::Info,
        }
    }

    pub fn message(mut self, message: &str) -> Self {
        self.message = String::from(message);
        self
    }

    pub fn timestamp(mut self, timestamp: Timestamp) -> Self {
        self.timestamp = timestamp;
        self
    }

    pub fn moniker(mut self, moniker: &str) -> Self {
        self.moniker = String::from(moniker);
        self
    }

    pub fn component_url(mut self, component_url: &str) -> Self {
        self.component_url = String::from(component_url);
        self
    }

    pub fn severity(mut self, severity: Severity) -> Self {
        self.severity = severity;
        self
    }

    pub fn build(&self) -> LogsData {
        let hierarchy = hierarchy! {
            root: {
                LogsField::Msg => self.message.clone(),
            }
        };
        LogsData::for_logs(
            self.moniker.clone(),
            Some(hierarchy),
            self.timestamp.clone(),
            self.component_url.clone(),
            self.severity.clone(),
            1,
            vec![],
        )
    }
}
