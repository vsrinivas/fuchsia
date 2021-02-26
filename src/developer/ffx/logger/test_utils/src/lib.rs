// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    async_std::sync::Arc,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorRequest,
    },
    futures::TryStreamExt,
};

pub struct FakeArchiveIteratorResponse {
    values: Vec<String>,
    iterator_error: Option<ArchiveIteratorError>,
}

impl FakeArchiveIteratorResponse {
    pub fn new_with_values(values: Vec<String>) -> Self {
        Self { values, iterator_error: None }
    }

    pub fn new_with_error(err: ArchiveIteratorError) -> Self {
        Self { values: vec![], iterator_error: Some(err) }
    }
}

pub fn setup_fake_archive_iterator(
    server_end: ServerEnd<ArchiveIteratorMarker>,
    responses: Arc<Vec<FakeArchiveIteratorResponse>>,
) -> Result<()> {
    let mut stream = server_end.into_stream()?;
    fuchsia_async::Task::spawn(async move {
        let mut iter = responses.iter();
        while let Ok(Some(req)) = stream.try_next().await {
            match req {
                ArchiveIteratorRequest::GetNext { responder } => {
                    let next = iter.next();
                    match next {
                        Some(FakeArchiveIteratorResponse { values, iterator_error }) => {
                            if let Some(err) = iterator_error {
                                responder.send(&mut Err(*err)).unwrap();
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
