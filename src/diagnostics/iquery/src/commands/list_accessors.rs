// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::types::*, types::Error},
    argh::FromArgs,
    async_trait::async_trait,
    fuchsia_zircon::DurationNum,
    futures::future::join_all,
    futures::stream::StreamExt,
    glob,
    lazy_static::lazy_static,
    regex::Regex,
    std::{collections::BTreeSet, path::PathBuf},
};

lazy_static! {
    static ref EXPECTED_FILE_RE: &'static str = r"fuchsia\.diagnostics\..*ArchiveAccessor$";
    static ref READDIR_TIMEOUT_SECONDS: i64 = 15;
}

/// Lists all ArchiveAccessor files under the provided paths. If no paths are provided, it'll list
/// under the current directory. At the moment v2 components cannot be seen through the filesystem.
/// Therefore this only outputs ArchiveAccessors exposed by v1 components.
#[derive(Default, FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list-accessors")]
pub struct ListAccessorsCommand {
    #[argh(positional)]
    /// glob paths from where to list files.
    pub paths: Vec<String>,
}

#[async_trait]
impl Command for ListAccessorsCommand {
    type Result = Vec<String>;

    async fn execute(&self) -> Result<Vec<String>, Error> {
        let paths = if self.paths.is_empty() {
            let path = std::env::current_dir()
                .map_err(|e| Error::io_error("Failed to get current dir", e.into()))?;
            vec![path.to_string_lossy().to_string()]
        } else {
            self.paths.clone()
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
}

async fn all_accessors(root: impl AsRef<str>) -> Result<Vec<String>, Error> {
    let dir_proxy =
        io_util::open_directory_in_namespace(root.as_ref(), io_util::OPEN_RIGHT_READABLE)
            .map_err(|e| Error::io_error(format!("Open dir {}", root.as_ref()), e))?;
    let expected_file_re = Regex::new(&EXPECTED_FILE_RE).unwrap();

    let paths = files_async::readdir_recursive(&dir_proxy, Some(READDIR_TIMEOUT_SECONDS.seconds()))
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
