// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{range::Range, resource::Resource},
    anyhow::Result,
    camino::Utf8PathBuf,
    fidl_fuchsia_developer_ffx_ext::RepositorySpec,
    futures::{future::BoxFuture, stream::BoxStream},
    std::{fmt::Debug, io, sync::Arc, time::SystemTime},
    tuf::{interchange::Json, repository::RepositoryProvider as TufRepositoryProvider},
    url::ParseError,
};

mod file_system;
mod gcs_repository;
mod http_repository;
mod pm;

#[cfg(test)]
pub(crate) mod repo_tests;

pub use file_system::FileSystemRepository;
pub use gcs_repository::GcsRepository;
pub use http_repository::HttpRepository;
pub use pm::PmRepository;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("not found")]
    NotFound,
    #[error("invalid path '{0}'")]
    InvalidPath(Utf8PathBuf),
    #[error("I/O error")]
    Io(#[source] io::Error),
    #[error("URL Parsing Error")]
    URLParseError(#[source] ParseError),
    #[error(transparent)]
    Tuf(#[from] tuf::Error),
    #[error(transparent)]
    Far(#[from] fuchsia_archive::Error),
    #[error(transparent)]
    Meta(#[from] fuchsia_pkg::MetaContentsError),
    #[error(transparent)]
    Http(#[from] http::uri::InvalidUri),
    #[error(transparent)]
    Hyper(#[from] hyper::Error),
    #[error(transparent)]
    ParseInt(#[from] std::num::ParseIntError),
    #[error(transparent)]
    ToStr(#[from] hyper::header::ToStrError),
    #[error(transparent)]
    MirrorConfig(#[from] fidl_fuchsia_pkg_ext::MirrorConfigError),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
    #[error("range not satisfiable")]
    RangeNotSatisfiable,
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

impl From<ParseError> for Error {
    fn from(err: ParseError) -> Self {
        Error::URLParseError(err)
    }
}

pub trait RepoProvider: TufRepositoryProvider<Json> + Debug + Send + Sync {
    /// Get a [RepositorySpec] for this [Repository]
    fn spec(&self) -> RepositorySpec;

    /// Fetch a metadata [Resource] from this repository.
    fn fetch_metadata_range<'a>(
        &'a self,
        path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>>;

    /// Fetch a blob [Resource] from this repository.
    fn fetch_blob_range<'a>(
        &'a self,
        path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>>;

    /// Whether or not the backend supports watching for file changes.
    fn supports_watch(&self) -> bool {
        false
    }

    /// Returns a stream which sends a unit value every time the given path is modified.
    fn watch(&self) -> anyhow::Result<BoxStream<'static, ()>> {
        Err(anyhow::anyhow!("Watching not supported for this repo type"))
    }

    /// Get the length of a blob in this repository.
    fn blob_len<'a>(&'a self, path: &str) -> BoxFuture<'a, anyhow::Result<u64>>;

    /// Get the modification time of a blob in this repository if available.
    fn blob_modification_time<'a>(
        &'a self,
        path: &str,
    ) -> BoxFuture<'a, anyhow::Result<Option<SystemTime>>>;
}

macro_rules! impl_provider {
    (
        <$($desc:tt)+
    ) => {
        impl <$($desc)+ {
            fn spec(&self) -> RepositorySpec {
                (**self).spec()
            }

            fn fetch_metadata_range<'a>(
                &'a self,
                path: &str,
                range: Range,
            ) -> BoxFuture<'a, Result<Resource, Error>> {
                (**self).fetch_metadata_range(path, range)
            }

            fn fetch_blob_range<'a>(
                &'a self,
                path: &str,
                range: Range,
            ) -> BoxFuture<'a, Result<Resource, Error>> {
                (**self).fetch_blob_range(path, range)
            }

            /// Whether or not the backend supports watching for file changes.
            fn supports_watch(&self) -> bool {
                (**self).supports_watch()
            }

            /// Returns a stream which sends a unit value every time the given path is modified.
            fn watch(&self) -> anyhow::Result<BoxStream<'static, ()>> {
                (**self).watch()
            }

            /// Get the length of a blob in this repository.
            fn blob_len<'a>(&'a self, path: &str) -> BoxFuture<'a, anyhow::Result<u64>> {
                (**self).blob_len(path)
            }

            /// Get the modification time of a blob in this repository if available.
            fn blob_modification_time<'a>(
                &'a self,
                path: &str,
            ) -> BoxFuture<'a, anyhow::Result<Option<SystemTime>>> {
                (**self).blob_modification_time(path)
            }
        }
    };
}

impl_provider!(<T: RepoProvider> RepoProvider for &T);
impl_provider!(<T: RepoProvider> RepoProvider for &mut T);
impl_provider!(<T: RepoProvider + ?Sized> RepoProvider for Box<T>);
impl_provider!(<T: RepoProvider + ?Sized> RepoProvider for Arc<T>);
