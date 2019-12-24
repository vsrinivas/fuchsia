use {
    anyhow::Error,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, NodeMarker, OPEN_RIGHT_READABLE},
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObjTrait},
    fuchsia_inspect::{self as inspect, Node, NumericProperty, StringProperty, UintProperty},
    fuchsia_vfs_pseudo_fs::{
        directory::entry::DirectoryEntry, file::asynchronous::read_only, pseudo_directory,
    },
    fuchsia_zircon as zx,
    std::{collections::BTreeMap, future::Future, path::PathBuf},
};

const STORAGE_INSPECT_FILE_NAME: &'static str = "storage_stats.inspect";

/// Publish stats on global storage as inspect file nodes at the configured paths.
pub fn add_stats_nodes<SvcObj: ServiceObjTrait>(
    fs: &mut ServiceFs<SvcObj>,
    dirs: &BTreeMap<String, String>,
) -> Result<(), Error> {
    for (inspect_path, directory_path) in dirs {
        let dir_path = PathBuf::from(directory_path);
        fs.add_remote(inspect_path, storage_inspect_proxy(inspect_path, dir_path)?);
    }
    Ok(())
}

// Returns a DirectoryProxy that contains a dynamic inspect file with stats on files stored under
// `path`.
fn storage_inspect_proxy(name: &str, path: PathBuf) -> Result<DirectoryProxy, Error> {
    let (proxy, server) = create_proxy::<DirectoryMarker>()?;
    let name = name.to_string(); // need to allocate before passing to async block
    fasync::spawn(async move {
        publish_data_directory_stats(name, path, server.into_channel().into()).await;
    });

    Ok(proxy)
}

fn publish_data_directory_stats(
    name: String,
    path: PathBuf,
    server_end: ServerEnd<NodeMarker>,
) -> impl Future {
    let mut dir = pseudo_directory! {
        STORAGE_INSPECT_FILE_NAME => read_only(
            // we clone the arguments because this macro may run this closure repeatedly
            move || get_data_directory_stats(name.clone(), path.clone()))
    };

    dir.open(OPEN_RIGHT_READABLE, 0, &mut std::iter::empty(), server_end);

    dir
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
async fn get_data_directory_stats(name: String, path: PathBuf) -> Result<Vec<u8>, zx::Status> {
    let inspector = inspect::Inspector::new_with_size(1024 * 1024); // 1MB buffer.

    let proxy = io_util::open_directory_in_namespace(
        &path.to_string_lossy(),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .or(Err(zx::Status::INTERNAL))?;

    let mut file_and_size = files_async::readdir_recursive(&proxy)
        .await
        .or(Err(zx::Status::INTERNAL))?
        .into_iter()
        .map(|val| {
            let pb = PathBuf::from(val.name);
            let meta = path.join(&pb).metadata().ok();
            (pb, meta)
        })
        .filter_map(|entry| match entry {
            (pb, Some(meta)) => {
                if meta.is_file() {
                    Some((pb, meta.len()))
                } else {
                    None
                }
            }
            _ => None,
        })
        .collect::<Vec<_>>();

    file_and_size.sort_by(|a, b| a.0.cmp(&b.0));

    struct Entry {
        node: Node,
        size_property: UintProperty,
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

    let stats = inspector.root().create_child("stats");
    let mut error_props: Vec<StringProperty> = vec![];

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

    let _total_stats = match get_info(path.to_string_lossy().to_string()).await {
        Ok(info) => Some(add_filesystem_stats(&stats, info)),
        Err(val) => {
            error_props.push(stats.create_string("error", val));
            None
        }
    };

    // TODO: this doesn't handle full NodeHierarchies
    inspector.copy_vmo_data().ok_or(zx::Status::INTERNAL)
}

struct TotalStats {
    _total_bytes: UintProperty,
    _used_bytes: UintProperty,
}

fn add_filesystem_stats(stats: &Node, info: fidl_fuchsia_io::FilesystemInfo) -> TotalStats {
    // Total bytes includes the actual size of the filesystem and shared bytes that can be used between
    // filesystems. We aren't out of space until we've exhausted both.
    TotalStats {
        _total_bytes: stats
            .create_uint("total_bytes", info.total_bytes + info.free_shared_pool_bytes),
        _used_bytes: stats.create_uint("used_bytes", info.used_bytes),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync,
        fuchsia_inspect::{
            assert_inspect_tree,
            reader::{NodeHierarchy, PartialNodeHierarchy},
        },
        futures::TryStreamExt,
        io_util,
        std::{convert::TryFrom, fs::File, io::prelude::*},
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

        let val = get_data_directory_stats("test_data".to_string(), tempdir.path().join("data"))
            .await
            .expect("get data");
        let hierarchy = PartialNodeHierarchy::try_from(val).expect("data is not an inspect file");
        assert_inspect_tree!(hierarchy,
        root: {
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
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn publish_data_directory_stats_test() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("test.txt")).expect("make file");

        let (proxy, server) =
            create_proxy::<DirectoryMarker>().expect("failed to create directoryproxy");

        let path_clone = tempdir.path().to_path_buf();
        fasync::spawn(async move {
            publish_data_directory_stats(
                "test_data".to_string(),
                path_clone,
                server.into_channel().into(),
            )
            .await;
        });

        let f = io_util::open_file(
            &proxy,
            &PathBuf::from(STORAGE_INSPECT_FILE_NAME),
            OPEN_RIGHT_READABLE,
        )
        .expect("failed to open storage inspect file");

        let bytes = io_util::read_file_bytes(&f).await.expect("failed to read file");
        let hierarchy: NodeHierarchy =
            PartialNodeHierarchy::try_from(bytes).expect("file is not an inspect file").into();
        assert_inspect_tree!(hierarchy,
        root: {
            stats: contains {
                error: "Query failed"
            },
            test_data: contains {
                size:0u64,
                "test.txt": {
                    size: 0u64
                }
            }
        });

        assert!(proxy.describe().await.is_ok());
        assert!(f.describe().await.is_ok());
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_filesystem_info_from_admin_test() {
        let (proxy, server) = create_proxy::<fidl_fuchsia_io::DirectoryAdminMarker>()
            .expect("failed to create proxy");

        fasync::spawn(async move {
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
                        eprintln!("Error: Unknown request {:?}", r);
                        return;
                    }
                }
            }
        });

        let info =
            get_filesystem_info_from_admin(proxy).await.expect("failed to get filesystem info");
        let inspector = inspect::Inspector::new();
        let _props = add_filesystem_stats(inspector.root(), info);
        assert_inspect_tree!(inspector,
        root: {
            total_bytes: 1000u64,
            used_bytes: 200u64
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_filesystem_info_from_admin_failure_test() {
        let (proxy, server) = create_proxy::<fidl_fuchsia_io::DirectoryAdminMarker>()
            .expect("failed to create proxy");

        fasync::spawn(async move {
            let mut stream = server.into_stream().expect("failed to convert to stream");
            while let Some(request) = stream.try_next().await.expect("failed to unwrap request") {
                match request {
                    fidl_fuchsia_io::DirectoryAdminRequest::QueryFilesystem { responder } => {
                        responder.send(1, None).expect("failed to send response");
                    }
                    r => {
                        eprintln!("Error: Unknown request {:?}", r);
                        return;
                    }
                }
            }
        });

        assert_eq!(
            Err("Query returned error".to_string()),
            get_filesystem_info_from_admin(proxy).await
        );
    }
}
