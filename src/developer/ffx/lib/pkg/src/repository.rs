// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bytes::Bytes,
    futures::Stream,
    std::{io, path::PathBuf, pin::Pin},
};

mod file_system;
mod manager;

pub use file_system::FileSystemRepository;
pub use manager::RepositoryManager;

#[derive(Debug)]
pub enum Error {
    NotFound,
    InvalidPath(PathBuf),
    Io(io::Error),
    Other(anyhow::Error),
}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Self {
        if err.kind() == std::io::ErrorKind::NotFound {
            Error::NotFound
        } else {
            Error::Io(err)
        }
    }
}

/// [Resource] represents some resource as a stream of [Bytes] as provided from
/// a repository server.
pub struct Resource {
    /// The length of the file in bytes.
    pub len: u64,

    /// A stream of bytes representing the resource.
    pub stream: Pin<Box<dyn Stream<Item = io::Result<Bytes>> + Send + Unpin + 'static>>,
}

impl std::fmt::Debug for Resource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("resource").field("len", &self.len).field("stream", &"..").finish()
    }
}

#[async_trait::async_trait]
pub trait Repository: std::fmt::Debug {
    /// The name of the repository.
    fn name(&self) -> &str;

    /// Fetch a [Resource] from this repository.
    async fn fetch(&self, path: &str) -> Result<Resource, Error>;
}
