// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Stream-based Fuchsia directory watcher.
//!
//! This module provides a higher-level asyncronous directory watcher that supports recursive
//! watching.

#![deny(missing_docs)]

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_io::NodeInfo,
    fuchsia_async as fasync,
    fuchsia_vfs_watcher::Watcher,
    futures::{
        channel::mpsc::{channel, Sender},
        future::{BoxFuture, FutureExt},
        sink::SinkExt,
        stream::{BoxStream, StreamExt, TryStreamExt},
    },
    io_util::{open_node_in_namespace, OPEN_RIGHT_READABLE},
    std::path::{Path, PathBuf},
};

/// The type of the given path.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum NodeType {
    /// The type of the path is unknown. It either could not be described or the returned value is
    /// not known to the crate.
    Unknown,

    /// The node is a file, or something that looks like a file.
    File,

    /// The node is a directory, or something that looks like a directory.
    Directory,
}

impl NodeType {
    /// Obtain the NodeType for a Path.
    ///
    /// Returns NodeType::Unknown on failure or unknown node type.
    ///
    /// This method is non-blocking.
    ///
    /// NOTE: Only NodeInfo::File and Vmofile are considered files.
    /// NOTE: Only NodeInfo::Directory is considered a directory.
    /// NOTE: Any other NodeInfo types are considered NodeType unknown.
    pub async fn from_path(path: impl AsRef<Path>) -> Result<Self, Error> {
        NodeType::inner_from_path(path.as_ref()).await
    }

    async fn inner_from_path(path: &Path) -> Result<Self, Error> {
        let path_as_str = path.to_str().ok_or(format_err!(
            "Path requested for watch is non-utf8 and our
 non-blocking directory apis require utf8 paths: {:?}.",
            path
        ))?;
        let dir_proxy = open_node_in_namespace(path_as_str, OPEN_RIGHT_READABLE)?;
        Ok(match dir_proxy.describe().await {
            Ok(NodeInfo::Directory(_)) => NodeType::Directory,
            Ok(NodeInfo::File(_)) | Ok(NodeInfo::Vmofile(_)) => NodeType::File,
            _ => NodeType::Unknown,
        })
    }
}

/// An event on a particular path on the file system.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PathEvent {
    /// A watcher observed the node being added.
    Added(PathBuf, NodeType),

    /// A watcher observed the node existing when watching started.
    Existing(PathBuf, NodeType),

    /// A watcher observed the path being removed.
    Removed(PathBuf),
}

/// PathEvents are convertable to their wrapped path.
impl AsRef<Path> for PathEvent {
    fn as_ref(&self) -> &Path {
        match self {
            PathEvent::Added(filename, _) => &filename,
            PathEvent::Existing(filename, _) => &filename,
            PathEvent::Removed(filename) => &filename,
        }
    }
}

/// Watches the given path for file changes.
///
/// Returns a stream of PathEvents if a watcher could be installed on the path successfully.
///
/// TODO(fxbug.dev/27279): This should generate a stream rather than spawning tasks.
pub async fn watch(path: impl Into<PathBuf>) -> Result<BoxStream<'static, PathEvent>, Error> {
    inner_watch(path.into()).await
}

async fn inner_watch(path: PathBuf) -> Result<BoxStream<'static, PathEvent>, Error> {
    let path_as_str = path.to_str().ok_or(format_err!(
        "Path requested for watch is non-utf8 and our
 non-blocking directory apis require utf8 paths: {:?}.",
        path
    ))?;
    let dir_proxy =
        io_util::open_directory_in_namespace(path_as_str, io_util::OPEN_RIGHT_READABLE)?;
    let (mut tx, rx) = channel(1);
    let mut watcher = Watcher::new(dir_proxy).await?;

    let path_future = async move {
        while let Ok(message) = watcher.try_next().await {
            let message = match message {
                Some(value) => value,
                None => {
                    break;
                }
            };

            if message.filename.as_os_str() == "." {
                continue;
            }

            let file_path = path.join(message.filename);

            let value = match message.event {
                fuchsia_vfs_watcher::WatchEvent::EXISTING => {
                    let node_type = match NodeType::from_path(&file_path).await {
                        Ok(t) => t,
                        Err(_) => {
                            continue;
                        }
                    };
                    PathEvent::Existing(file_path, node_type)
                }
                fuchsia_vfs_watcher::WatchEvent::ADD_FILE => {
                    let node_type = match NodeType::from_path(&file_path).await {
                        Ok(t) => t,
                        Err(_) => {
                            continue;
                        }
                    };
                    PathEvent::Added(file_path, node_type)
                }
                fuchsia_vfs_watcher::WatchEvent::REMOVE_FILE => PathEvent::Removed(file_path),
                _ => {
                    continue;
                }
            };

            if tx.send(value).await.is_err() {
                return;
            };
        }
    };

    fasync::Task::spawn(path_future).detach();

    Ok(rx.boxed() as BoxStream<'_, _>)
}

/// Watches a path and all directories under that path for changes.
///
/// Returns a stream of PathEvent results. The individual results may
/// contain Errors as well as valid results. All wrapped paths are relative to the given initial
/// path.
///
/// The stream ends when the given path is deleted.
pub fn watch_recursive(path: impl Into<PathBuf>) -> BoxStream<'static, Result<PathEvent, Error>> {
    let (tx, rx) = channel(1);

    fasync::Task::spawn(inner_watch_recursive(tx, path.into(), true)).detach();

    rx.boxed()
}

// Internal function for watching a path.
//
// tx: The sender for the path event stream.
// path: The path to watch.
// existing: True if the path was existing when the top-most watch_recursive was called, false
// otherwise.
//
// Returns a boxed future that must be polled to process events into the sender stream.
fn inner_watch_recursive(
    mut tx: Sender<Result<PathEvent, Error>>,
    path: PathBuf,
    existing: bool,
) -> BoxFuture<'static, ()> {
    async move {
        let mut watch_stream = match watch(&path).await {
            Ok(stream) => stream,
            Err(e) => {
                tx.send(Err(e)).await.unwrap_or_else(|_| {});
                return;
            }
        };

        while let Some(event) = watch_stream.next().await {
            let (next_path, next_existing, send_event) = match event {
                PathEvent::Added(path, node_type) => {
                    // A path was observed being added, we need to watch the new path if it is a
                    // directory
                    (
                        if node_type == NodeType::Directory { Some(path.clone()) } else { None },
                        false, /*existing*/
                        PathEvent::Added(path, node_type),
                    )
                }
                PathEvent::Existing(path, node_type) => {
                    let next_path =
                        if node_type == NodeType::Directory { Some(path.clone()) } else { None };
                    let send_event = if existing {
                        PathEvent::Existing(path, node_type)
                    } else {
                        PathEvent::Added(path, node_type)
                    };

                    (next_path, existing, send_event)
                }
                PathEvent::Removed(path) => (None, existing, PathEvent::Removed(path)),
            };
            if tx.send(Ok(send_event)).await.is_err() {
                break;
            }
            if let Some(next_path) = next_path {
                fasync::Task::spawn(inner_watch_recursive(tx.clone(), next_path, next_existing))
                    .detach();
            }
        }
    }
    .boxed()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn path_event_conversion() {
        assert_eq!(
            Path::new("test/a"),
            PathEvent::Added("test/a".into(), NodeType::Unknown).as_ref()
        );
        assert_eq!(
            Path::new("test/a"),
            PathEvent::Existing("test/a".into(), NodeType::Unknown).as_ref()
        );
        assert_eq!(Path::new("test/a"), PathEvent::Removed("test/a".into()).as_ref());
    }

    #[fasync::run_singlethreaded(test)]
    async fn watch_single_directory() {
        let dir = tempdir().expect("create temp dir");
        let path = dir.path();

        let existing_path = path.join("existing.txt");
        let file1 = path.join("file1.txt");
        let subdir = path.join("subdir");

        fs::write(&existing_path, "a").expect("write existing");
        let mut watch_stream = watch(path.clone()).await.expect("watch stream");
        fs::write(&file1, "a").expect("write file1");

        assert_eq!(
            PathEvent::Existing(existing_path.clone(), NodeType::File),
            watch_stream.next().await.expect("existing path read")
        );
        assert_eq!(
            PathEvent::Added(file1.clone(), NodeType::File),
            watch_stream.next().await.expect("added file read")
        );

        // Create subdir and a file under it.
        fs::create_dir(&subdir).unwrap();
        fs::write(subdir.join("not_seen.txt"), "a").expect("write not seen");

        // Remove the existing file and file1.
        fs::remove_file(&existing_path).expect("remove existing");
        fs::remove_file(&file1).expect("remove file1");

        // Recreate and remove file1, we will fail to describe this file and its type will be
        // Unknown.
        fs::write(&file1, "a").expect("write file1");
        fs::remove_file(&file1).expect("remove file1 again");

        assert_eq!(
            PathEvent::Added(subdir.clone(), NodeType::Directory),
            watch_stream.next().await.expect("added subdir read")
        );
        assert_eq!(
            PathEvent::Removed(existing_path.clone()),
            watch_stream.next().await.expect("removed existing path read")
        );
        assert_eq!(
            PathEvent::Removed(file1.clone()),
            watch_stream.next().await.expect("removed file1 read")
        );
        assert_eq!(
            PathEvent::Added(file1.clone(), NodeType::Unknown),
            watch_stream.next().await.expect("added file1 read")
        );
        assert_eq!(
            PathEvent::Removed(file1.clone()),
            watch_stream.next().await.expect("removed file1 again read")
        );

        // Remove everything, ensuring we reach the end of the stream.
        fs::remove_dir_all(path).expect("remove all");

        assert_eq!(
            PathEvent::Removed(subdir.clone()),
            watch_stream.next().await.expect("removed subdir read")
        );
        assert_eq!(None, watch_stream.next().await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn watch_error() {
        let dir = tempdir().expect("make tempdir");
        let path = dir.path();

        assert!(watch(path.join("does_not_exist")).await.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn watch_recursive_directories() {
        let dir = tempdir().expect("make tempdir");
        let path = dir.path();

        let existing_dir = path.join("existing");
        let existing_file = existing_dir.join("file.txt");
        let subdir = path.join("subdir");
        let subsubdir = subdir.join("sub");

        fs::create_dir(&existing_dir).expect("create existing");
        fs::write(&existing_file, "a").expect("write existing file");
        let mut watch_stream = watch_recursive(path.clone());

        assert_eq!(
            Some(PathEvent::Existing(existing_dir.clone(), NodeType::Directory)),
            watch_stream.try_next().await.expect("existing dir read")
        );
        assert_eq!(
            Some(PathEvent::Existing(existing_file.clone(), NodeType::File)),
            watch_stream.try_next().await.expect("existing file read")
        );

        // Create subdir and a directory under it.
        // The second directory will be seen as "existing" by the recursive watcher, but will
        // inherit the fact that its parent was observed being added, so it will be added as well.
        fs::create_dir(&subdir).expect("make subdir");
        fs::create_dir(&subsubdir).expect("make subsubdir");

        assert_eq!(
            Some(PathEvent::Added(subdir.clone(), NodeType::Directory)),
            watch_stream.try_next().await.expect("added subdir read")
        );
        assert_eq!(
            Some(PathEvent::Added(subsubdir.clone(), NodeType::Directory)),
            watch_stream.try_next().await.expect("added subsubdir read")
        );

        // Remove the existing dir and everything under it.
        fs::remove_dir_all(&existing_dir).expect("remove existing dir");

        assert_eq!(
            Some(PathEvent::Removed(existing_file.clone())),
            watch_stream.try_next().await.expect("removed existing file read")
        );
        assert_eq!(
            Some(PathEvent::Removed(existing_dir.clone())),
            watch_stream.try_next().await.expect("removed existing dir read")
        );

        // Recreate and remove existing dir, we will fail to describe this directory, and attaching
        // a watcher will fail.
        // Unknown.
        fs::create_dir(&existing_dir).expect("create existing dir");
        fs::remove_dir_all(&existing_dir).expect("remove existing dir");

        assert_eq!(
            Some(PathEvent::Added(existing_dir.clone(), NodeType::Unknown)),
            watch_stream.try_next().await.expect("added existing dir read")
        );
        assert_eq!(
            Some(PathEvent::Removed(existing_dir.clone())),
            watch_stream.try_next().await.expect("removed existing dir again read")
        );

        // Remove everything, ensuring we reach the end of the stream.
        fs::remove_dir_all(path).expect("remove all");

        assert_eq!(
            Some(PathEvent::Removed(subsubdir.clone())),
            watch_stream.try_next().await.expect("removed subsubdir read")
        );
        assert_eq!(
            Some(PathEvent::Removed(subdir.clone())),
            watch_stream.try_next().await.expect("removed subdir read")
        );
        assert_eq!(None, watch_stream.try_next().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn watch_recursive_error() {
        let dir = tempdir().expect("create tempdir");
        let path = dir.path();

        let file = path.join("file");

        fs::write(&file, "a").expect("create file");

        let mut stream = watch_recursive(&file);

        assert!(stream.try_next().await.is_err());
        assert_eq!(None, stream.try_next().await.expect("read sentinel"));
    }
}
