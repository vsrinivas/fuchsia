// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::items::ClipboardItem,
    derivative::Derivative,
    fuchsia_inspect::{self as finspect, health::Reporter, NumericProperty, Property},
    fuchsia_zircon as zx,
    std::convert::TryInto,
    std::{
        cell::RefCell,
        collections::BTreeMap,
        rc::{Rc, Weak},
    },
    tracing::warn,
};

/// Types of events that can be tallied by the Inspect implementation
#[derive(Debug, Copy, Clone, PartialOrd, Ord, PartialEq, Eq, Hash)]
pub(crate) enum EventType {
    Read,
    Write,
    Clear,
    ReadAccessDenied,
    /// Includes unauthorized attempts to clear the clipboard.
    WriteAccessDenied,
    /// Other error besides access denied.
    WriteError,
    FocusUpdated,
}

impl std::fmt::Display for EventType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        use EventType::*;
        let s = match self {
            Read => "read",
            Write => "write",
            Clear => "clear",
            ReadAccessDenied => "read_access_denied",
            WriteAccessDenied => "write_access_denied",
            WriteError => "write_error",
            FocusUpdated => "focus_updated",
        };
        write!(f, "{s}")
    }
}

/// Holds inspect data for [`crate::service::Service`].
#[derive(Debug)]
pub(crate) struct ServiceInspectData {
    inner: Rc<Inner>,
}

#[derive(Derivative)]
#[derivative(Debug)]
struct Inner {
    _node: finspect::Node,
    #[derivative(Debug = "ignore")]
    health: RefCell<finspect::health::Node>,
    events_node: finspect::Node,
    events_by_type: RefCell<BTreeMap<EventType, EventCounter>>,
    /// Number of currently connected `FocusedReaderRegistry` clients
    reader_registry_client_count: finspect::UintProperty,
    /// Number of currently connected `FocusedWriterRegistry` clients
    writer_registry_client_count: finspect::UintProperty,
    /// Number of currently connected readers
    reader_count: finspect::UintProperty,
    /// Number of currently connected writers
    writer_count: finspect::UintProperty,
    /// Container for items metadata
    items_node: finspect::Node,
    /// Metadata about the items
    items: RefCell<BTreeMap<String, ItemInspectData>>,
    /// Timestamp of the last modification of the clipboard contents
    last_modified_ns: finspect::IntProperty,
}

impl ServiceInspectData {
    /// # Arguments
    /// -   `root`: Value of `inspector().root()`. Overridable for testing.
    /// -   `now`: Timestamp at which the inspect data (and the whole clipboard service) was
    ///     instantiated.
    pub fn new(root: &finspect::Node, now: zx::Time) -> Self {
        let node = root.create_child("clipboard");
        let health = RefCell::new(finspect::health::Node::new(root));
        let events_node = node.create_child("events");
        let events_by_type = RefCell::new(BTreeMap::new());
        let reader_registry_client_count = node.create_uint("reader_registry_client_count", 0);
        let writer_registry_client_count = node.create_uint("writer_registry_client_count", 0);
        let reader_count = node.create_uint("reader_count", 0);
        let writer_count = node.create_uint("writer_count", 0);
        let items_node = node.create_child("items");
        let items = RefCell::new(BTreeMap::new());
        let last_modified_ns = node.create_int("last_modified_ns", now.into_nanos());
        Self {
            inner: Rc::new(Inner {
                _node: node,
                health,
                events_node,
                events_by_type,
                reader_registry_client_count,
                writer_registry_client_count,
                reader_count,
                writer_count,
                items_node,
                items,
                last_modified_ns,
            }),
        }
    }

    /// Records that the service is healthy.
    pub fn set_healthy(&self) {
        self.inner.health.borrow_mut().set_ok();
    }

    /// Records that the service is unhealthy, with a required explanatory message.
    pub fn set_unhealthy(&self, msg: &str) {
        self.inner.health.borrow_mut().set_unhealthy(msg);
    }

    /// Records an occurrence of one of the supported event types.
    pub fn record_event(&self, event_type: EventType, now: zx::Time) {
        let mut events_by_type = self.inner.events_by_type.borrow_mut();
        let event_counter = events_by_type
            .entry(event_type)
            .or_insert_with(|| EventCounter::new(&self.inner.events_node, event_type));
        event_counter.event_count.add(1);
        event_counter.last_seen_ns.set(now.into_nanos());
    }

    /// Increases the count of reader registry clients by 1. It will decrease by 1 when the
    /// returned `InstanceCounter` is dropped.
    #[must_use]
    pub fn scoped_increment_reader_registry_client_count(&self) -> InstanceCounter {
        InstanceCounter::new(self, |d| &d.reader_registry_client_count)
    }

    /// Increases the count of writer registry clients by 1. It will decrease by 1 when the
    /// returned `InstanceCounter` is dropped.
    #[must_use]
    pub fn scoped_increment_writer_registry_client_count(&self) -> InstanceCounter {
        InstanceCounter::new(self, |d| &d.writer_registry_client_count)
    }

    /// Increases the count of readers by 1. It will decrease by 1 when the returned
    /// `InstanceCounter` is dropped.
    #[must_use]
    pub fn scoped_increment_reader_count(&self) -> InstanceCounter {
        InstanceCounter::new(self, |d| &d.reader_count)
    }

    /// Increases the count of writers by 1. It will decrease by 1 when the returned
    /// `InstanceCounter` is dropped.
    #[must_use]
    pub fn scoped_increment_writer_count(&self) -> InstanceCounter {
        InstanceCounter::new(self, |d| &d.writer_count)
    }

    /// Records metadata about a current clipboard item. If `replace_all` is `true`, replaces all
    /// existing items with the new one.
    ///
    /// This method updates [`last_modified_ns`].
    pub fn record_item(&self, item: &ClipboardItem, replace_all: bool, now: zx::Time) {
        if let Ok(mut items) = self.inner.items.try_borrow_mut() {
            if replace_all {
                items.clear();
            }
            let item_data = ItemInspectData::new(&self.inner.items_node, item);
            let mime_type = item.mime_type_hint().to_string();
            items.insert(mime_type, item_data);
            self.update_last_modified(now);
        } else {
            warn!("Items map was borrowed, couldn't update inspect tree");
        }
    }

    /// Records that the clipboard's items have been cleared.
    ///
    /// This method updates [`last_modified_ns`].
    pub fn clear_items(&self, now: zx::Time) {
        if let Ok(mut items) = self.inner.items.try_borrow_mut() {
            items.clear();
            self.update_last_modified(now);
        } else {
            warn!("Items map was borrowed, couldn't update inspect tree");
        }
    }

    fn update_last_modified(&self, now: zx::Time) {
        self.inner.last_modified_ns.set(now.into_nanos());
    }
}

/// Exposes metadata about a clipboard item.
#[derive(Debug)]
struct ItemInspectData {
    _node: finspect::Node,
    /// Size of the payload in bytes
    _bytes: finspect::UintProperty,
}

impl ItemInspectData {
    fn new(items_node: &finspect::Node, item: &ClipboardItem) -> Self {
        let node = items_node.create_child(item.mime_type_hint());
        let bytes = node.create_uint(
            "size_bytes",
            item.payload_size_bytes().try_into().expect("payload size should fit into u64"),
        );
        ItemInspectData { _node: node, _bytes: bytes }
    }
}

/// Keeps track of occurrences of an event type.
#[derive(Debug)]
struct EventCounter {
    /// Node representing this event type's properties.
    _node: finspect::Node,
    event_count: finspect::UintProperty,
    last_seen_ns: finspect::IntProperty,
}

impl EventCounter {
    fn new(events_node: &finspect::Node, event_type: EventType) -> Self {
        let node = events_node.create_child(format!("{event_type}"));
        let event_count = node.create_uint("event_count", 0);
        let last_seen_ns = node.create_int("last_seen_ns", 0);
        EventCounter { _node: node, event_count, last_seen_ns }
    }
}

type UintPropertyAccessor = fn(&Inner) -> &finspect::UintProperty;

/// Adds 1 to the given property when instantiated, subtracts 1 when dropped.
pub(crate) struct InstanceCounter {
    parent_inner: Weak<Inner>,
    property_accessor: UintPropertyAccessor,
}

impl InstanceCounter {
    fn new(parent: &ServiceInspectData, property_accessor: UintPropertyAccessor) -> Self {
        property_accessor(&parent.inner).add(1);
        Self { parent_inner: Rc::downgrade(&parent.inner), property_accessor }
    }
}

impl Drop for InstanceCounter {
    fn drop(&mut self) {
        if let Some(parent_inner) = self.parent_inner.upgrade() {
            (self.property_accessor)(&*parent_inner).subtract(1);
        }
        // Nothing to do if ServiceInspectData was already dropped
    }
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        fuchsia_inspect::{assert_data_tree, Inspector},
        fuchsia_zircon as zx,
    };

    /// # Arguments
    /// -   `now`: Initialization timestamp.
    fn make_handles(now: zx::Time) -> (ServiceInspectData, Inspector) {
        let inspector = Inspector::new();
        let inspect_data = ServiceInspectData::new(inspector.root(), now);
        (inspect_data, inspector)
    }

    #[test]
    fn test_health() {
        let (inspect_data, inspector) = make_handles(zx::Time::from_nanos(1));

        assert_data_tree!(inspector, root: contains {
            "fuchsia.inspect.Health": contains {
                status: "STARTING_UP"
            }
        });

        inspect_data.set_healthy();
        assert_data_tree!(inspector, root: contains {
            "fuchsia.inspect.Health": contains {
                status: "OK"
            }
        });

        inspect_data.set_unhealthy("Ouch");
        assert_data_tree!(inspector, root: contains {
            "fuchsia.inspect.Health": contains {
                status: "UNHEALTHY",
                message: "Ouch"
            }
        });
    }

    #[test]
    fn test_record_event() {
        let (inspect_data, inspector) = make_handles(zx::Time::from_nanos(1));

        inspect_data.record_event(EventType::Write, zx::Time::from_nanos(17));
        inspect_data.record_event(EventType::Write, zx::Time::from_nanos(34));

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                events: contains {
                    write: {
                        event_count: 2u64,
                        last_seen_ns: 34i64,
                    }
                },
                // Should not be updated by `record_event()`.
                last_modified_ns: 1i64,
            }
        });
    }

    #[test]
    fn test_counters() {
        let (inspect_data, inspector) = make_handles(zx::Time::from_nanos(1));

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                reader_registry_client_count: 0u64,
                writer_registry_client_count: 0u64,
                reader_count: 0u64,
                writer_count: 0u64,
                last_modified_ns: 1i64,
            }
        });

        let _rr1 = inspect_data.scoped_increment_reader_registry_client_count();
        let _wr1 = inspect_data.scoped_increment_writer_registry_client_count();
        {
            let _rr2 = inspect_data.scoped_increment_reader_registry_client_count();
            let _r1 = inspect_data.scoped_increment_reader_count();
            let _w1 = inspect_data.scoped_increment_writer_count();
            let _w2 = inspect_data.scoped_increment_writer_count();

            assert_data_tree!(inspector, root: contains {
                clipboard: contains {
                    reader_registry_client_count: 2u64,
                    writer_registry_client_count: 1u64,
                    reader_count: 1u64,
                    writer_count: 2u64,
                    last_modified_ns: 1i64,
                }
            });
        }

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                reader_registry_client_count: 1u64,
                writer_registry_client_count: 1u64,
                reader_count: 0u64,
                writer_count: 0u64,
                last_modified_ns: 1i64,
            }
        });
    }

    #[test]
    fn test_record_item() {
        let (inspect_data, inspector) = make_handles(zx::Time::from_nanos(1));

        let item1 = ClipboardItem::new_text_item("🐕🐈", "foo/bar");
        inspect_data.record_item(&item1, false, zx::Time::from_nanos(17));

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                items: {
                    "foo/bar": {
                        size_bytes: 8u64,
                    }
                },
                last_modified_ns: 17i64,
            }
        });

        let item2 = ClipboardItem::new_text_item("cat dog", "another/mime");
        inspect_data.record_item(&item2, false, zx::Time::from_nanos(34));

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                items: {
                    "foo/bar": {
                        size_bytes: 8u64,
                    },
                    "another/mime": {
                        size_bytes: 7u64,
                    }
                },
                last_modified_ns: 34i64,
            }
        });

        let item3 = ClipboardItem::new_text_item("#", "foo/bar");
        inspect_data.record_item(&item3, false, zx::Time::from_nanos(51));

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                items: {
                    "foo/bar": {
                        size_bytes: 1u64,
                    },
                    "another/mime": {
                        size_bytes: 7u64,
                    }
                },
                last_modified_ns: 51i64,
            }
        });

        let item3 = ClipboardItem::new_text_item("abcd", "foo/bar");
        inspect_data.record_item(&item3, true, zx::Time::from_nanos(68));

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                items: {
                    "foo/bar": {
                        size_bytes: 4u64,
                    },
                },
                last_modified_ns: 68i64,
            }
        });
    }

    #[test]
    fn test_clear_items() {
        let (inspect_data, inspector) = make_handles(zx::Time::from_nanos(1));

        let item1 = ClipboardItem::new_text_item("🐕🐈", "foo/bar");
        inspect_data.record_item(&item1, false, zx::Time::from_nanos(17));

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                items: {
                    "foo/bar": {
                        size_bytes: 8u64,
                    }
                },
                last_modified_ns: 17i64,
            }
        });

        inspect_data.clear_items(zx::Time::from_nanos(34));

        assert_data_tree!(inspector, root: contains {
            clipboard: contains {
                items: {},
                last_modified_ns: 34i64,
            }
        });
    }
}
