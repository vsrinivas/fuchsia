// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fidl::endpoints::ServerEnd,
    fidl::prelude::*,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorRequest,
        DiagnosticsData, InlineData,
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
    legacy_format: bool,
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
                                        .into_iter()
                                        .map(|s| {
                                            if legacy_format {
                                                ArchiveIteratorEntry {
                                                    data: Some(s.clone()),
                                                    truncated_chars: Some(0),
                                                    ..ArchiveIteratorEntry::EMPTY
                                                }
                                            } else {
                                                ArchiveIteratorEntry {
                                                    diagnostics_data: Some(
                                                        DiagnosticsData::Inline(InlineData {
                                                            data: s.clone(),
                                                            truncated_chars: 0,
                                                        }),
                                                    ),
                                                    ..ArchiveIteratorEntry::EMPTY
                                                }
                                            }
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
