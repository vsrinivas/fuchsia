// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::types::DiagnosticsProvider, types::Error},
    async_trait::async_trait,
    diagnostics_data::{Data, DiagnosticsData},
    diagnostics_reader::ArchiveReader,
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, ArchiveAccessorProxy},
    fidl_fuchsia_io as fio,
    fuchsia_component::client,
    fuchsia_fs::directory::DirentKind,
    fuchsia_zircon::DurationNum,
    futures::future::join_all,
    futures::StreamExt,
    glob,
    lazy_static::lazy_static,
    regex::Regex,
    std::{collections::BTreeSet, path::PathBuf},
};

lazy_static! {
    static ref EXPECTED_FILE_RE: &'static str = r"fuchsia\.diagnostics\..*ArchiveAccessor$";
    static ref READDIR_TIMEOUT_SECONDS: i64 = 15;
}

#[derive(Default)]
pub struct ArchiveAccessorProvider {}

#[async_trait]
impl DiagnosticsProvider for ArchiveAccessorProvider {
    async fn snapshot<D>(
        &self,
        accessor_path: &Option<String>,
        selectors: &[String],
    ) -> Result<Vec<Data<D>>, Error>
    where
        D: DiagnosticsData,
    {
        let archive = connect_to_archive_accessor(accessor_path).await?;
        let selectors = selectors.iter().map(|s| s.as_ref());
        ArchiveReader::new()
            .with_archive(archive)
            .retry_if_empty(false)
            .add_selectors(selectors)
            .snapshot::<D>()
            .await
            .map_err(|e| Error::Fetch(e))
    }

    async fn get_accessor_paths(&self, paths: &Vec<String>) -> Result<Vec<String>, Error> {
        get_accessor_paths(paths).await
    }
}

/// If no path is provided, then connects to the global archivist. If one is provided, then
/// connects to first `ArchiveAccessor` that the given glob matches or returns an error if none
/// is found.
pub async fn connect_to_archive_accessor(
    accessor_path: &Option<String>,
) -> Result<ArchiveAccessorProxy, Error> {
    match accessor_path {
        None => client::connect_to_protocol::<ArchiveAccessorMarker>()
            .map_err(|e| Error::ConnectToArchivist(e)),
        Some(path) => connect_to_archive_at(path).await,
    }
}

/// Connects to first `ArchiveAccessor` that the given glob matches.
async fn connect_to_archive_at(glob_path: &str) -> Result<ArchiveAccessorProxy, Error> {
    let path_results =
        glob::glob(&glob_path).map_err(|e| Error::ParsePath(glob_path.to_string(), e.into()))?;
    for path_result in path_results {
        if let Ok(path) = path_result {
            let path_str = path.to_string_lossy().to_string();
            let node =
                fuchsia_fs::node::open_in_namespace(&path_str, fio::OpenFlags::NODE_REFERENCE)
                    .map_err(|e| Error::io_error("open node in namespace", e.into()))?;
            if let Ok(node_info) = node.describe_deprecated().await {
                match node_info {
                    fio::NodeInfoDeprecated::Service(_) => {
                        return client::connect_to_protocol_at_path::<ArchiveAccessorMarker>(
                            &path_str,
                        )
                        .map_err(|e| Error::ConnectToArchivist(e));
                    }
                    fio::NodeInfoDeprecated::Directory(_) => {
                        let directory = fuchsia_fs::directory::open_in_namespace(
                            &path_str,
                            fio::OpenFlags::RIGHT_READABLE,
                        )
                        .map_err(|e| Error::io_error("open directory in namespace", e.into()))?;
                        let mut stream = fuchsia_fs::directory::readdir_recursive(&directory, None);
                        while let Some(result) = stream.next().await {
                            if let Ok(entry) = result {
                                if entry.kind == DirentKind::Service
                                    && entry.name.ends_with(ArchiveAccessorMarker::PROTOCOL_NAME)
                                {
                                    let accessor_path = format!("{}/{}", path_str, entry.name);
                                    return client::connect_to_protocol_at_path::<
                                        ArchiveAccessorMarker,
                                    >(&accessor_path)
                                    .map_err(|e| Error::ConnectToArchivist(e));
                                }
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
    }
    Err(Error::UnknownArchivePath)
}

/// Lists all ArchiveAccessor files under the provided paths. If no paths are provided, it'll list
/// under the current directory. At the moment v2 components cannot be seen through the filesystem.
/// Therefore this only outputs ArchiveAccessors exposed by v1 components.
pub async fn get_accessor_paths(paths: &Vec<String>) -> Result<Vec<String>, Error> {
    let paths = if paths.is_empty() {
        let path = std::env::current_dir()
            .map_err(|e| Error::io_error("Failed to get current dir", e.into()))?;
        vec![path.to_string_lossy().to_string()]
    } else {
        paths.clone()
    };

    let mut result = BTreeSet::new();
    for query_path in paths {
        let path_futs = glob::glob(&query_path)
            .map_err(|e| Error::ParsePath(query_path.clone(), e.into()))?
            .map(|path_result| match path_result {
                Err(e) => Err(Error::ParsePath(query_path.clone(), e.into())),
                Ok(path) => Ok(all_accessors(path.to_string_lossy().to_string())),
            })
            .collect::<Result<Vec<_>, _>>()?;
        for paths_result in join_all(path_futs).await {
            let paths =
                paths_result.map_err(|e| Error::ListAccessors(query_path.clone(), e.into()))?;
            result.extend(paths.into_iter());
        }
    }
    Ok(result.into_iter().collect::<Vec<_>>())
}

async fn all_accessors(root: impl AsRef<str>) -> Result<Vec<String>, Error> {
    let dir_proxy = fuchsia_fs::directory::open_in_namespace(
        root.as_ref(),
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .map_err(|e| Error::io_error(format!("Open dir {}", root.as_ref()), e.into()))?;
    let expected_file_re = Regex::new(&EXPECTED_FILE_RE).unwrap();

    let paths = fuchsia_fs::directory::readdir_recursive(
        &dir_proxy,
        Some(READDIR_TIMEOUT_SECONDS.seconds()),
    )
    .filter_map(|result| async {
        match result {
            Err(err) => {
                eprintln!("{}", err);
                None
            }
            Ok(entry) => {
                if expected_file_re.is_match(&entry.name) {
                    let mut path = PathBuf::from(root.as_ref());
                    path.push(&entry.name);
                    Some(path.to_string_lossy().to_string())
                } else {
                    None
                }
            }
        }
    })
    .collect::<Vec<String>>()
    .await;
    Ok(paths)
}
