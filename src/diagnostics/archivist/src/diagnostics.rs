use {
    crate::archive::EventFileGroupStatsMap,
    anyhow::Error,
    fuchsia_component::server::{ServiceFs, ServiceObjTrait},
    fuchsia_inspect::{component, Node, UintProperty},
    lazy_static::lazy_static,
    parking_lot::Mutex,
    std::sync::Arc,
};

lazy_static! {
    static ref GROUPS: Arc<Mutex<Groups>> = Arc::new(Mutex::new(Groups::new(
        component::inspector().root().create_child("archived_events")
    )));
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

pub fn init() {
    //TODO(36574): Replace log calls once archivist can use LogSink service.
}

pub fn root() -> &'static Node {
    component::inspector().root()
}

pub fn serve(service_fs: &mut ServiceFs<impl ServiceObjTrait>) -> Result<(), Error> {
    component::inspector().serve(service_fs)
}

pub(crate) fn set_group_stats(stats: &EventFileGroupStatsMap) {
    GROUPS.lock().replace(stats);
}

#[cfg(test)]
mod test {
    use {
        super::*, crate::archive::EventFileGroupStats, fuchsia_inspect::assert_inspect_tree,
        fuchsia_inspect::health::Reporter, std::iter::FromIterator,
    };

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
        let inspector = fuchsia_inspect::Inspector::new();
        let mut group = Groups::new(inspector.root().create_child("archived_events"));
        group.replace(&EventFileGroupStatsMap::from_iter(vec![
            ("a/b".to_string(), EventFileGroupStats { file_count: 1, size: 2 }),
            ("c/d".to_string(), EventFileGroupStats { file_count: 3, size: 4 }),
        ]));

        assert_inspect_tree!(inspector,
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
}
