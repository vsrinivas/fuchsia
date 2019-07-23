use {
    crate::archive::{EventFileGroupStats, EventFileGroupStatsMap},
    fuchsia_component::server::{ServiceFs, ServiceObjTrait},
    fuchsia_inspect::{component, Node, NumericProperty, Property, UintProperty},
    lazy_static::lazy_static,
    std::path::Path,
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
    fuchsia_syslog::init_with_tags(&["archivist"]).expect("can't init logger");
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);
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

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect::health::Reporter;
    use std::iter::FromIterator;
    use std::path::PathBuf;

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

}
