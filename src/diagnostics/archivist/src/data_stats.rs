// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_zircon as zx,
    futures::{FutureExt, StreamExt},
    std::{collections::BTreeMap, path::PathBuf},
};

/// Publish stats on global storage as inspect file nodes at the configured paths.
pub fn add_stats_nodes(
    root_node: &inspect::Node,
    dirs: BTreeMap<String, String>,
) -> Result<(), Error> {
    let stats_node = root_node.create_child("data_stats");
    for (name, directory_path) in dirs {
        stats_node.record_lazy_child(name.clone(), move || {
            let name_clone = name.clone();
            let directory_path_clone = directory_path.clone();
            async move {
                let dir_path = PathBuf::from(directory_path_clone);
                get_data_directory_stats(name_clone, dir_path)
                    .await
                    .map_err(|e| format_err!("Error: {:?}", e))
            }
            .boxed()
        });
    }
    root_node.record(stats_node);
    Ok(())
}

async fn get_filesystem_info_from_admin(
    proxy: fidl_fuchsia_io::DirectoryAdminProxy,
) -> Result<fidl_fuchsia_io::FilesystemInfo, String> {
    let (status, info) =
        proxy.query_filesystem().await.or_else(|_| Err("Query failed".to_string()))?;
    if status != 0 {
        return Err("Query returned error".to_string());
    }

    info.ok_or_else(|| "FilesystemInfo wasn't present".to_string()).map(|v| *v)
}

/// Traverses `path` recursively and populates the returned inspect VMO with stats on the contents.
async fn get_data_directory_stats(
    name: String,
    path: PathBuf,
) -> Result<inspect::Inspector, zx::Status> {
    let inspector = inspect::Inspector::new_with_size(1024 * 1024); // 1MB buffer.

    let proxy = io_util::open_directory_in_namespace(
        &path.to_string_lossy(),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .or(Err(zx::Status::INTERNAL))?;

    let mut file_and_size = files_async::readdir_recursive(&proxy, None)
        .filter_map(|result| async {
            match result {
                Ok(entry) => {
                    let pb = PathBuf::from(entry.name);
                    let maybe_meta = path.join(&pb).metadata().ok();
                    if let Some(meta) = maybe_meta {
                        if meta.is_file() {
                            return Some((pb, meta.len()));
                        }
                    }
                    None
                }
                _ => {
                    // Errors occur when reading dirs. We skip reading this dir.
                    // TODO(fxbug.dev/49157): consider showing an error in the output if we failed to
                    // read this dir.
                    None
                }
            }
        })
        .collect::<Vec<_>>()
        .await;

    file_and_size.sort_by(|a, b| a.0.cmp(&b.0));

    struct Entry {
        node: inspect::Node,
        size_property: inspect::UintProperty,
        children: BTreeMap<String, Box<Entry>>,
    }

    let node = inspector.root().create_child(name);
    let size_property = node.create_uint("size", 0);
    let mut top = Box::new(Entry { node, size_property, children: BTreeMap::new() });

    for (pb, size) in file_and_size.iter() {
        top.size_property.add(*size);

        let mut cur = &mut top;
        for next in pb {
            let next = next.to_string_lossy().into_owned();
            if !cur.children.contains_key(&next) {
                let node = cur.node.create_child(&next);
                let size_property = node.create_uint("size", 0);
                cur.children.insert(
                    next.clone(),
                    Box::new(Entry { node, size_property, children: BTreeMap::new() }),
                );
            }
            cur = cur.children.get_mut(&next).expect("find just inserted value");
            cur.size_property.add(*size);
        }
    }

    let mut entries = vec![top];
    while let Some(entry) = entries.pop() {
        entry.node.record(entry.size_property);
        inspector.root().record(entry.node);
        for (_, child_entry) in entry.children {
            entries.push(child_entry);
        }
    }

    async fn get_info(path: String) -> Result<fidl_fuchsia_io::FilesystemInfo, String> {
        let proxy = io_util::open_directory_in_namespace(
            &path,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_ADMIN,
        )
        .or_else(|e| Err(format!("Could not get admin access {:?}", e)))?;

        let proxy = fidl_fuchsia_io::DirectoryAdminProxy::new(
            proxy.into_channel().or_else(|e| Err(format!("Could not extract channel {:?}", e)))?,
        );

        get_filesystem_info_from_admin(proxy).await
    }

    let stats = inspector.root().create_child("stats");
    match get_info(path.to_string_lossy().to_string()).await {
        Ok(info) => {
            add_filesystem_stats(&stats, info);
        }
        Err(val) => {
            stats.record_string("error", val);
        }
    };
    inspector.root().record(stats);

    Ok(inspector)
}

fn add_filesystem_stats(stats: &inspect::Node, info: fidl_fuchsia_io::FilesystemInfo) {
    // Total bytes includes the actual size of the filesystem and shared bytes that can be used between
    // filesystems. We aren't out of space until we've exhausted both.
    stats.record_uint("total_bytes", info.total_bytes + info.free_shared_pool_bytes);
    stats.record_uint("used_bytes", info.used_bytes);
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy,
        fuchsia_async as fasync,
        fuchsia_inspect::{assert_inspect_tree, reader},
        futures::TryStreamExt,
        std::{fs::File, io::prelude::*},
        tempfile::TempDir,
    };

    #[fasync::run_singlethreaded(test)]
    async fn get_data_directory_stats_test() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("my_file.txt"))
            .expect("create file")
            .write_all(b"Hello")
            .expect("writing test file");
        std::fs::create_dir_all(tempdir.path().join("data/a/b")).expect("make data/a/b");
        std::fs::create_dir_all(tempdir.path().join("data/a/c/d")).expect("make data/a/c/d");

        for f in
            ["data/top.txt", "data/a/a.txt", "data/a/b/b.txt", "data/a/c/c.txt", "data/a/c/d/d.txt"]
                .iter()
        {
            File::create(tempdir.path().join(f))
                .expect(format!("create file {}", f).as_ref())
                .write_all(f.as_bytes())
                .expect(format!("writing {}", f).as_ref());
        }

        let mut dirs = BTreeMap::new();
        dirs.insert(
            "test_data".to_string(),
            tempdir.path().join("data").to_string_lossy().to_string(),
        );

        let inspector = inspect::Inspector::new();
        add_stats_nodes(inspector.root(), dirs).expect("get data");
        let hierarchy = reader::read_from_inspector(&inspector).await.expect("get hierarchy");

        assert_inspect_tree!(hierarchy,
        root: {
            data_stats: {
                test_data: {
                    stats: contains {
                        error: "Query failed"
                    },
                    test_data: {
                        size: 68u64,
                        "top.txt": {
                            size: 12u64
                        },
                        a: {
                            size: 56u64,
                            "a.txt": {
                                size: 12u64
                            },
                            b: {
                                size: 14u64,
                                "b.txt": {
                                    size: 14u64,
                                }
                            },
                            c: {
                                size: 30u64,
                                "c.txt": {
                                    size: 14u64
                                },
                                d: {
                                    size: 16u64,
                                    "d.txt": {
                                        size: 16u64
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn publish_data_directory_stats_test() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("test.txt")).expect("make file");

        let mut dirs = BTreeMap::new();
        dirs.insert("test_data".to_string(), tempdir.path().to_string_lossy().to_string());

        let inspector = inspect::Inspector::new();
        add_stats_nodes(inspector.root(), dirs).expect("get data");
        let hierarchy = reader::read_from_inspector(&inspector).await.expect("get hierarchy");

        assert_inspect_tree!(hierarchy,
        root: {
            data_stats: {
                test_data: {
                    stats: contains {
                        error: "Query failed"
                    },
                    test_data: contains {
                        size:0u64,
                        "test.txt": {
                            size: 0u64
                        }
                    }
                }
            }
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_filesystem_info_from_admin_test() {
        let (proxy, server) = create_proxy::<fidl_fuchsia_io::DirectoryAdminMarker>()
            .expect("failed to create proxy");

        fasync::Task::spawn(async move {
            let mut stream = server.into_stream().expect("failed to convert to stream");
            while let Some(request) = stream.try_next().await.expect("failed to unwrap request") {
                match request {
                    fidl_fuchsia_io::DirectoryAdminRequest::QueryFilesystem { responder } => {
                        responder
                            .send(
                                0,
                                Some(&mut fidl_fuchsia_io::FilesystemInfo {
                                    total_bytes: 800,
                                    used_bytes: 200,
                                    total_nodes: 0,
                                    used_nodes: 0,
                                    free_shared_pool_bytes: 200,
                                    fs_id: 0,
                                    block_size: 4096,
                                    max_filename_size: 128,
                                    fs_type: 0,
                                    padding: 0,
                                    name: ['a' as i8; 32],
                                }),
                            )
                            .expect("failed to send response");
                    }
                    r => {
                        panic!("Unknown request {:?}", r);
                    }
                }
            }
        })
        .detach();

        let info =
            get_filesystem_info_from_admin(proxy).await.expect("failed to get filesystem info");
        let inspector = inspect::Inspector::new();
        add_filesystem_stats(inspector.root(), info);
        let hierarchy = reader::read_from_inspector(&inspector).await.expect("get hierarchy");

        assert_inspect_tree!(hierarchy,
        root: {
            total_bytes: 1000u64,
            used_bytes: 200u64
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_filesystem_info_from_admin_failure_test() {
        let (proxy, server) = create_proxy::<fidl_fuchsia_io::DirectoryAdminMarker>()
            .expect("failed to create proxy");

        fasync::Task::spawn(async move {
            let mut stream = server.into_stream().expect("failed to convert to stream");
            while let Some(request) = stream.try_next().await.expect("failed to unwrap request") {
                match request {
                    fidl_fuchsia_io::DirectoryAdminRequest::QueryFilesystem { responder } => {
                        responder.send(1, None).expect("failed to send response");
                    }
                    r => {
                        panic!("Unknown request {:?}", r);
                    }
                }
            }
        })
        .detach();

        assert_eq!(
            Err("Query returned error".to_string()),
            get_filesystem_info_from_admin(proxy).await
        );
    }
}
