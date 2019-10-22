#![allow(dead_code)]

use {
    crate::archive::{EventFileGroupStats, EventFileGroupStatsMap},
    core::future::Future,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, OPEN_RIGHT_READABLE},
    files_async,
    fuchsia_component::server::{ServiceFs, ServiceObjTrait},
    fuchsia_inspect::{
        self as inspect, component, Node, NumericProperty, Property, StringProperty, UintProperty,
    },
    fuchsia_vfs_pseudo_fs::{
        directory::entry::DirectoryEntry, file::asynchronous::read_only, pseudo_directory,
    },
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    std::collections::BTreeMap,
    std::path::{Path, PathBuf},
    std::sync::{Arc, Mutex},
};

lazy_static! {
    static ref GROUPS: Arc<Mutex<Groups>> = Arc::new(Mutex::new(Groups::new(
        component::inspector().root().create_child("archived_events")
    )));
    static ref STATS: Arc<Mutex<Stats>> = Arc::new(Mutex::new(Stats::new(
        component::inspector().root().create_child("archive_stats")
    )));
    static ref CURRENT_GROUP_NODE: Node =
        component::inspector().root().create_child("current_group");
    static ref CURRENT_GROUP: Arc<Mutex<Option<Stats>>> = Arc::new(Mutex::new(None));
}

const STORAGE_INSPECT_FILE_NAME: &'static str = "storage_stats.inspect";

enum GroupData {
    Node(Node),
    Count(UintProperty),
}

struct Groups {
    node: Node,
    children: Vec<GroupData>,
}

impl Groups {
    fn new(node: Node) -> Self {
        Groups { node, children: vec![] }
    }

    fn replace(&mut self, stats: &EventFileGroupStatsMap) {
        self.children.clear();
        for (name, stat) in stats {
            let node = self.node.create_child(name);
            let files = node.create_uint("file_count", stat.file_count as u64);
            let size = node.create_uint("size_in_bytes", stat.size);

            self.children.push(GroupData::Node(node));
            self.children.push(GroupData::Count(files));
            self.children.push(GroupData::Count(size));
        }
    }
}

struct Stats {
    node: Node,
    total_bytes: UintProperty,
    total_files: UintProperty,
}

impl Stats {
    fn new(node: Node) -> Self {
        let total_bytes = node.create_uint("size_in_bytes", 0);
        let total_files = node.create_uint("file_count", 0);
        Stats { node, total_bytes, total_files }
    }
}

pub fn init() {
    //TODO(36574): Replace log calls once archivist can use LogSink service.
}

pub fn root() -> &'static Node {
    component::inspector().root()
}

pub fn export(service_fs: &mut ServiceFs<impl ServiceObjTrait>) {
    component::inspector().export(service_fs);
}

pub fn set_group_stats(stats: &EventFileGroupStatsMap) {
    let mut groups = GROUPS.lock().unwrap();
    groups.replace(stats);
}

pub fn add_stats(group_stats: &EventFileGroupStats) {
    let stats = STATS.lock().unwrap();
    stats.total_bytes.add(group_stats.size);
    stats.total_files.add(group_stats.file_count as u64);
}

pub fn subtract_stats(group_stats: &EventFileGroupStats) {
    let stats = STATS.lock().unwrap();
    stats.total_bytes.subtract(group_stats.size);
    stats.total_files.subtract(group_stats.file_count as u64);
}

pub fn set_current_group(name: &Path, stats: &EventFileGroupStats) {
    {
        let mut current_group = CURRENT_GROUP.lock().unwrap();
        *current_group =
            Some(Stats::new(CURRENT_GROUP_NODE.create_child(name.to_string_lossy().to_string())));
    }
    update_current_group(stats);
}

pub fn update_current_group(stats: &EventFileGroupStats) {
    let mut current_group = CURRENT_GROUP.lock().unwrap();
    match current_group.as_mut() {
        Some(stats_node) => {
            stats_node.total_bytes.set(stats.size);
            stats_node.total_files.set(stats.file_count as u64);
        }
        None => {}
    }
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

struct TotalStats {
    total_bytes: UintProperty,
    used_bytes: UintProperty,
}

fn add_filesystem_stats(stats: &Node, info: fidl_fuchsia_io::FilesystemInfo) -> TotalStats {
    // Total bytes includes the actual size of the filesystem and shared bytes that can be used between
    // filesystems. We aren't out of space until we've exhausted both.
    TotalStats {
        total_bytes: stats
            .create_uint("total_bytes", info.total_bytes + info.free_shared_pool_bytes),
        used_bytes: stats.create_uint("used_bytes", info.used_bytes),
    }
}

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

    for val in file_and_size.iter() {
        let (pb, size) = val;

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

    inspector.copy_vmo_data().ok_or(zx::Status::INTERNAL)
}

pub fn publish_data_directory_stats(
    name: String,
    path: PathBuf,
    server_end: ServerEnd<NodeMarker>,
) -> impl Future {
    let mut dir = pseudo_directory! {
        STORAGE_INSPECT_FILE_NAME => read_only(
            move || get_data_directory_stats(name.clone(), path.clone()))
    };

    dir.open(OPEN_RIGHT_READABLE, 0, &mut std::iter::empty(), server_end);

    dir
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_io::DirectoryMarker;
    use fuchsia_async as fasync;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect::health::Reporter;
    use fuchsia_inspect::reader::NodeHierarchy;
    use futures::TryStreamExt;
    use io_util;
    use std::convert::TryFrom;
    use std::fs::File;
    use std::io::prelude::*;
    use std::iter::FromIterator;
    use tempfile::TempDir;

    #[test]
    fn health() {
        component::health().set_ok();
        assert_inspect_tree!(component::inspector(),
        root: contains {
            "fuchsia.inspect.Health": {
                status: "OK",
            }
        });

        component::health().set_unhealthy("Bad state");
        assert_inspect_tree!(component::inspector(),
        root: contains {
            "fuchsia.inspect.Health": {
                status: "UNHEALTHY",
                message: "Bad state",
            }
        });

        component::health().set_ok();
        assert_inspect_tree!(component::inspector(),
        root: contains {
            "fuchsia.inspect.Health": {
                status: "OK",
            }
        });
    }

    #[test]
    fn group_stats() {
        set_group_stats(&EventFileGroupStatsMap::from_iter(vec![
            ("a/b".to_string(), EventFileGroupStats { file_count: 1, size: 2 }),
            ("c/d".to_string(), EventFileGroupStats { file_count: 3, size: 4 }),
        ]));
        assert_inspect_tree!(component::inspector(),
        root: contains {
            archived_events: {
               "a/b": {
                    file_count: 1u64,
                    size_in_bytes: 2u64
               },
               "c/d": {
                   file_count: 3u64,
                   size_in_bytes: 4u64
               }
            }
        });
    }

    #[test]
    fn current_group_stats() {
        let path1 = PathBuf::from("a/b");
        let path2 = PathBuf::from("c/d");
        set_current_group(&path1, &EventFileGroupStats { file_count: 1, size: 2 });
        assert_inspect_tree!(component::inspector(),
        root: contains {
            current_group: {
                "a/b": {
                    file_count: 1u64,
                    size_in_bytes: 2u64
                }
            }
        });

        update_current_group(&EventFileGroupStats { file_count: 3, size: 4 });
        assert_inspect_tree!(component::inspector(),
        root: contains {
            current_group: {
                "a/b": {
                    file_count: 3u64,
                    size_in_bytes: 4u64
                }
            }
        });

        set_current_group(&path2, &EventFileGroupStats { file_count: 4, size: 5 });
        assert_inspect_tree!(component::inspector(),
        root: contains {
            current_group: {
                "c/d": {
                    file_count: 4u64,
                    size_in_bytes: 5u64
                }
            }
        });
    }

    #[test]
    fn archive_stats() {
        add_stats(&EventFileGroupStats { file_count: 1, size: 10 });
        assert_inspect_tree!(component::inspector(),
        root: contains {
            archive_stats: {
                file_count: 1u64,
                size_in_bytes: 10u64,
            }
        });

        add_stats(&EventFileGroupStats { file_count: 3, size: 30 });
        assert_inspect_tree!(component::inspector(),
        root: contains {
            archive_stats: {
                file_count: 4u64,
                size_in_bytes: 40u64,
            }
        });

        subtract_stats(&EventFileGroupStats { file_count: 2, size: 20 });
        assert_inspect_tree!(component::inspector(),
        root: contains {
            archive_stats: {
                file_count: 2u64,
                size_in_bytes: 20u64,
            }
        });
    }

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
                .into_iter()
        {
            File::create(tempdir.path().join(f))
                .expect(format!("create file {}", f).as_ref())
                .write_all(f.as_bytes())
                .expect(format!("writing {}", f).as_ref());
        }

        let val = get_data_directory_stats("test_data".to_string(), tempdir.path().join("data"))
            .await
            .expect("get data");
        assert_inspect_tree!(NodeHierarchy::try_from(val).expect("data is not an inspect file"),
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
        assert_inspect_tree!(
            NodeHierarchy::try_from(bytes).expect("file is not an inspect file"),
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
                                Some(fidl::encoding::OutOfLine(
                                    &mut fidl_fuchsia_io::FilesystemInfo {
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
                                    },
                                )),
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
