// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{range::ContentRange, repository::Error, util::read_stream_to_end},
    bytes::Bytes,
    fidl_fuchsia_pkg_ext::RepositoryConfig,
    futures::{
        future::ready,
        stream::{once, BoxStream, StreamExt},
    },
    std::{convert::TryFrom, io},
};

/// [Resource] represents some resource as a stream of [Bytes] as provided from
/// a repository server.
pub struct Resource {
    /// The range in bytes available for this resource.
    pub content_range: ContentRange,

    /// A stream of bytes representing the resource.
    pub stream: BoxStream<'static, io::Result<Bytes>>,
}

impl Resource {
    /// The length of the content in bytes in the stream. This may be smaller than the total length
    /// of the file.
    pub fn content_len(&self) -> u64 {
        self.content_range.content_len()
    }

    /// The total length of the file range in bytes. This may be larger than the bytes in the stream.
    pub fn total_len(&self) -> u64 {
        self.content_range.total_len()
    }

    pub async fn read_to_end(&mut self, buf: &mut Vec<u8>) -> Result<(), Error> {
        buf.reserve(self.content_len() as usize);
        read_stream_to_end(&mut self.stream, buf).await.map_err(Error::Io)
    }
}

impl std::fmt::Debug for Resource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("resource")
            .field("content_range", &self.content_range)
            .field("stream", &"..")
            .finish()
    }
}

impl TryFrom<RepositoryConfig> for Resource {
    type Error = Error;

    fn try_from(config: RepositoryConfig) -> Result<Resource, Error> {
        let json = Bytes::from(serde_json::to_vec(&config).map_err(|e| anyhow::anyhow!(e))?);
        let complete_len = json.len() as u64;
        Ok(Resource {
            content_range: ContentRange::Full { complete_len },
            stream: once(ready(Ok(json))).boxed(),
        })
    }
}
