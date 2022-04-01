// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::repository::{Error, RepositoryConfig},
    bytes::Bytes,
    futures::{
        future::ready,
        stream::{once, BoxStream, StreamExt},
    },
    std::{convert::TryFrom, io},
};

/// [Resource] represents some resource as a stream of [Bytes] as provided from
/// a repository server.
pub struct Resource {
    /// The length of the file range in bytes.
    pub content_len: u64,

    /// The length of the total file in bytes.
    pub total_len: u64,

    /// A stream of bytes representing the resource.
    pub stream: BoxStream<'static, io::Result<Bytes>>,
}

impl Resource {
    pub async fn read_to_end(&mut self, buf: &mut Vec<u8>) -> Result<(), Error> {
        buf.reserve(self.content_len as usize);
        while let Some(chunk) = self.stream.next().await.transpose()? {
            buf.extend_from_slice(&chunk);
        }
        Ok(())
    }
}

impl std::fmt::Debug for Resource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("resource")
            .field("content_len", &self.content_len)
            .field("total_len", &self.total_len)
            .field("stream", &"..")
            .finish()
    }
}

impl TryFrom<RepositoryConfig> for Resource {
    type Error = Error;
    fn try_from(config: RepositoryConfig) -> Result<Resource, Error> {
        let json = Bytes::from(serde_json::to_vec(&config).map_err(|e| anyhow::anyhow!(e))?);
        let len = json.len() as u64;
        Ok(Resource { content_len: len, total_len: len, stream: once(ready(Ok(json))).boxed() })
    }
}

#[derive(Debug, Clone)]
pub enum ResourceRange {
    RangeFull,
    Range { start: u64, end: u64 },
    RangeFrom { start: u64 },
    RangeTo { end: u64 },
}
