// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    anyhow::{anyhow, bail, Error, Result},
    fuchsia_fs::directory::DirentKind,
    std::path::{Component, PathBuf},
};

/// Returns a valid file path to write to given a source and destination path.
///
/// The purpose of this function is to help infer a path in cases which an ending file name for the destination path is not provided.
/// For example, the command "ffx component storage copy ~/alarm.wav [instance-id]::/" does not know what name to give the new file copied.
/// [instance-id]::/. Thus it is necessary to infer this new file name and generate the new path "[instance-id]::/alarm.wav".
///
/// This function will check the current destination path and return a new path if the final component in the path is a directory.
/// The new path will have the filename of the source path appended to the destination path.
/// Otherwise, the destination path passed in will be returned.
///
/// # Arguments
/// * `source`:  source path of either a host filepath or a component namespace entry
/// * `destination`: source path of either a host filepath or a component namespace entry
/// * `storage_dir`: Directory proxy to help retrieve directory entries on device
///
/// # Error Conditions:
/// * One path must be a host filepath and the other must be a component namespace path
/// * File name for source could not be found
/// * File already exist at destination
/// * destination is not a directory
pub async fn finalize_destination_to_filepath(
    storage_dir: &Directory,
    source: HostOrRemotePath,
    destination: HostOrRemotePath,
) -> Result<PathBuf, Error> {
    let source_file = match source {
        HostOrRemotePath::Host(path) => path.file_name().map_or_else(
            || Err(anyhow!("Source path was unexpectedly empty")),
            |file| Ok(PathBuf::from(file)),
        )?,
        HostOrRemotePath::Remote(path) => path.relative_path.file_name().map_or_else(
            || Err(anyhow!("Source path was unexpectedly empty")),
            |file| Ok(PathBuf::from(file)),
        )?,
    };

    let source_file_str = source_file.display().to_string();
    // Checks to see if the destination is currently a directory. The source path file is appended to the destination if it is a directory.
    match destination {
        HostOrRemotePath::Host(mut destination_path) => {
            let destination_file = destination_path.file_name();

            if destination_file.is_none() || destination_path.is_dir() {
                destination_path.push(&source_file_str);
            }

            Ok(destination_path)
        }
        HostOrRemotePath::Remote(destination_path) => {
            let mut destination_path = destination_path.relative_path;
            let destination_file = destination_path.file_name();
            let remote_destination_entry = if destination_file.is_some() {
                storage_dir
                    .entry_if_exists(
                        destination_file.unwrap_or_default().to_str().unwrap_or_default(),
                    )
                    .await?
            } else {
                storage_dir.entry_if_exists(&source_file_str).await?
            };

            match (&remote_destination_entry, &destination_file) {
                (Some(remote_destination), _) => {
                    match remote_destination.kind {
                        DirentKind::File => {}
                        // TODO(https://fxrev.dev/745090): Update component_manager vfs to assign proper DirentKinds when installing the directory tree.
                        DirentKind::Directory | DirentKind::Unknown => {
                            destination_path.push(&source_file_str);
                        }
                        _ => {
                            return Err(anyhow!(
                                "Invalid entry type for file {}",
                                &source_file_str
                            ));
                        }
                    }
                    Ok(destination_path)
                }
                (None, Some(_)) => Ok(destination_path),
                (None, None) => {
                    destination_path.push(&source_file_str);
                    Ok(destination_path)
                }
            }
        }
    }
}

pub const REMOTE_PATH_HELP: &'static str = r#"Remote paths have the following formats:
1)  [instance ID]::[path relative to storage]
    Example: "c1a6d0aebbf7c092c53e8e696636af8ec0629ff39b7f2e548430b0034d809da4::/path/to/file"

    `..` is not valid anywhere in the remote path.

    To learn about component instance IDs, see https://fuchsia.dev/go/components/instance-id

2) [absolute moniker]::[path in namespace]
    Example: /foo/bar::/config/data/sample.json

   To learn more about absolute monikers, see https://fuchsia.dev/go/components/moniker#absolute"#;

pub struct RemotePath {
    pub remote_id: String,
    pub relative_path: PathBuf,
}

/// Represents a path to a component instance.
/// Refer to REMOTE_PATH_HELP above for the format of RemotePath.
impl RemotePath {
    pub fn parse(input: &str) -> Result<Self> {
        match input.split_once("::") {
            Some((first, second)) => {
                if second.contains("::") {
                    bail!(
                        "Remote path must contain exactly one `::` separator. {}",
                        REMOTE_PATH_HELP
                    )
                }

                let remote_id = first.to_string();
                let relative_path = PathBuf::from(second);

                // Perform checks on path that ignore `.`  and disallow `..`, `/` or Windows path prefixes such as C: or \\
                let mut normalized_relative_path = PathBuf::new();
                for component in relative_path.components() {
                    match component {
                        Component::Normal(c) => normalized_relative_path.push(c),
                        Component::RootDir => continue,
                        Component::CurDir => continue,
                        c => bail!("Unsupported path object: {:?}. {}", c, REMOTE_PATH_HELP),
                    }
                }

                Ok(Self { remote_id, relative_path: normalized_relative_path })
            }
            None => {
                bail!("Remote path must contain exactly one `::` separator. {}", REMOTE_PATH_HELP)
            }
        }
    }
}

pub enum HostOrRemotePath {
    Host(PathBuf),
    Remote(RemotePath),
}

impl HostOrRemotePath {
    pub fn parse(path: &str) -> HostOrRemotePath {
        match RemotePath::parse(path) {
            Ok(path) => HostOrRemotePath::Remote(path),
            // If we can't parse a remote path, then it is a host path.
            Err(_) => HostOrRemotePath::Host(PathBuf::from(path)),
        }
    }
}
