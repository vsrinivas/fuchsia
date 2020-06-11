// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::archive::EventFileGroupStatsMap,
    anyhow::Error,
    fuchsia_component::server::{ServiceFs, ServiceObjTrait},
    fuchsia_inspect::{component, health::Reporter, Node, UintProperty},
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
    component::health().set_starting_up();
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

pub struct ArchiveAccessorStats {
    /// Inspect node for tracking usage/health metrics of diagnostics platform.
    pub archive_accessor_node: fuchsia_inspect::Node,
    /// Property tracking number of opening connections to any archive_accessor instance.
    pub global_archive_accessor_connections_opened: fuchsia_inspect::UintProperty,
    /// Property tracking number of closing connections to any archive_accessor instance.
    pub global_archive_accessor_connections_closed: fuchsia_inspect::UintProperty,
    /// Number of requests to a single ArchiveAccessor to StreamDiagnostics, starting a
    /// new inspect ReaderServer.
    pub global_stream_diagnostics_requests: fuchsia_inspect::UintProperty,

    /// Number of inspect ReaderServers created in response to an inspect StreamDiagnostics
    /// client request.
    pub global_inspect_reader_servers_constructed: Arc<fuchsia_inspect::UintProperty>,
    /// Number of inspect ReaderServers destroyed in response to falling out of scope.
    pub global_inspect_reader_servers_destroyed: Arc<fuchsia_inspect::UintProperty>,
    /// Property tracking number of opening connections to any inspect batch iterator instance.
    pub global_inspect_batch_iterator_connections_opened: Arc<fuchsia_inspect::UintProperty>,
    /// Property tracking number of closing connections to any inspect batch iterator instance.
    pub global_inspect_batch_iterator_connections_closed: Arc<fuchsia_inspect::UintProperty>,
    /// Property tracking number of times a future to retrieve diagnostics data for a component
    /// timed out.
    pub global_component_timeouts_count: Arc<fuchsia_inspect::UintProperty>,
}

impl ArchiveAccessorStats {
    pub fn new(archive_accessor_node: fuchsia_inspect::Node) -> Self {
        let global_archive_accessor_connections_opened =
            archive_accessor_node.create_uint("archive_accessor_connections_opened", 0);
        let global_archive_accessor_connections_closed =
            archive_accessor_node.create_uint("archive_accessor_connections_closed", 0);

        let global_stream_diagnostics_requests =
            archive_accessor_node.create_uint("stream_diagnostics_requests", 0);

        let global_inspect_reader_servers_constructed =
            Arc::new(archive_accessor_node.create_uint("inspect_reader_servers_constructed", 0));
        let global_inspect_reader_servers_destroyed =
            Arc::new(archive_accessor_node.create_uint("inspect_reader_servers_destroyed", 0));

        let global_inspect_batch_iterator_connections_opened = Arc::new(
            archive_accessor_node.create_uint("inspect_batch_iterator_connections_opened", 0),
        );
        let global_inspect_batch_iterator_connections_closed = Arc::new(
            archive_accessor_node.create_uint("inspect_batch_iterator_connections_closed", 0),
        );
        let global_component_timeouts_count =
            Arc::new(archive_accessor_node.create_uint("component_timeouts_count", 0));

        ArchiveAccessorStats {
            archive_accessor_node,
            global_archive_accessor_connections_opened,
            global_archive_accessor_connections_closed,
            global_stream_diagnostics_requests,
            global_inspect_reader_servers_constructed,
            global_inspect_reader_servers_destroyed,
            global_inspect_batch_iterator_connections_opened,
            global_inspect_batch_iterator_connections_closed,
            global_component_timeouts_count,
        }
    }
}

pub struct InspectReaderServerStats {
    /// Inspect node for tracking usage/health metrics of a single connection to a batch iterator.
    _batch_iterator_connection_node: fuchsia_inspect::Node,

    /// Number of inspect ReaderServers created in response to an inspect StreamDiagnostics
    /// client request.
    pub global_inspect_reader_servers_constructed: Arc<fuchsia_inspect::UintProperty>,
    /// Number of inspect ReaderServers destroyed in response to falling out of scope.
    pub global_inspect_reader_servers_destroyed: Arc<fuchsia_inspect::UintProperty>,

    /// Property tracking number of opening connections to any inspect batch iterator instance.
    pub global_inspect_batch_iterator_connections_opened: Arc<fuchsia_inspect::UintProperty>,
    /// Property tracking number of closing connections to any inspect batch iterator instance.
    pub global_inspect_batch_iterator_connections_closed: Arc<fuchsia_inspect::UintProperty>,
    /// Property tracking number of times a future to retrieve diagnostics data for a component
    /// timed out.
    pub global_component_timeouts_count: Arc<fuchsia_inspect::UintProperty>,

    /// Property tracking number of requests to the BatchIterator instance this struct is tracking.
    pub batch_iterator_get_next_requests: fuchsia_inspect::UintProperty,
    /// Property tracking number of responses from the BatchIterator instance this struct is tracking.
    pub batch_iterator_get_next_responses: fuchsia_inspect::UintProperty,
    /// Property tracking number of times the batch iterator has served a terminal batch signalling that
    /// the client has reached the end of the iterator and should terminate their connection.
    pub batch_iterator_terminal_responses: fuchsia_inspect::UintProperty,
}

impl InspectReaderServerStats {
    pub fn new(archive_accessor_stats: Arc<ArchiveAccessorStats>) -> Self {
        let batch_iterator_connection_node = archive_accessor_stats
            .archive_accessor_node
            .create_child(fuchsia_inspect::unique_name("batch_iterator_connection"));

        let batch_iterator_get_next_requests =
            batch_iterator_connection_node.create_uint("batch_iterator_get_next_requests", 0);
        let batch_iterator_get_next_responses =
            batch_iterator_connection_node.create_uint("batch_iterator_get_next_responses", 0);
        let batch_iterator_terminal_responses =
            batch_iterator_connection_node.create_uint("batch_iterator_terminal_responses", 0);
        InspectReaderServerStats {
            _batch_iterator_connection_node: batch_iterator_connection_node,
            global_inspect_reader_servers_constructed: archive_accessor_stats
                .global_inspect_reader_servers_constructed
                .clone(),
            global_inspect_reader_servers_destroyed: archive_accessor_stats
                .global_inspect_reader_servers_destroyed
                .clone(),
            global_inspect_batch_iterator_connections_opened: archive_accessor_stats
                .global_inspect_batch_iterator_connections_opened
                .clone(),
            global_inspect_batch_iterator_connections_closed: archive_accessor_stats
                .global_inspect_batch_iterator_connections_closed
                .clone(),
            global_component_timeouts_count: archive_accessor_stats
                .global_component_timeouts_count
                .clone(),
            batch_iterator_get_next_requests,
            batch_iterator_get_next_responses,
            batch_iterator_terminal_responses,
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, crate::archive::EventFileGroupStats, fuchsia_inspect::assert_inspect_tree,
        fuchsia_inspect::health::Reporter, fuchsia_inspect::testing::AnyProperty,
        std::iter::FromIterator,
    };

    #[test]
    fn health() {
        component::health().set_ok();
        assert_inspect_tree!(component::inspector(),
        root: {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
            }
        });

        component::health().set_unhealthy("Bad state");
        assert_inspect_tree!(component::inspector(),
        root: contains {
            "fuchsia.inspect.Health": {
                status: "UNHEALTHY",
                message: "Bad state",
                start_timestamp_nanos: AnyProperty,
            }
        });

        component::health().set_ok();
        assert_inspect_tree!(component::inspector(),
        root: contains {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
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
