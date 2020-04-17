// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)] // TODO(50039) Use this.

use crate::inspect::BoundedNode;
use fuchsia_inspect::Node;
use fuchsia_zircon::{ClockId::Monotonic, Time};

#[derive(Debug)]
pub enum Event<'a> {
    ErrorCheckingForUpdate,
    NoUpdateAvailable,
    InstallationDeferredByPolicy { target_version: &'a str },
    InstallationError { target_version: &'a str },
    WaitingForReboot { target_version: &'a str },
}

impl Event<'_> {
    fn write_to_inspect(&self, node: &Node) {
        node.record_int("ts", Time::get(Monotonic).into_nanos());
        match self {
            Event::ErrorCheckingForUpdate => node.record_string("event", "ErrorCheckingForUpdate"),
            Event::NoUpdateAvailable => node.record_string("event", "NoUpdateAvailable"),
            Event::InstallationDeferredByPolicy { target_version } => {
                node.record_string("event", "InstallationDeferredByPolicy");
                node.record_string("target-version", target_version);
            }
            Event::InstallationError { target_version } => {
                node.record_string("event", "InstallationError");
                node.record_string("target-version", target_version);
            }
            Event::WaitingForReboot { target_version } => {
                node.record_string("event", "WaitingForReboot");
                node.record_string("target-version", target_version);
            }
        }
    }
}

#[derive(Debug)]
pub struct Emitter {
    events: BoundedNode<Node>,
    _node: Node,
}

impl Emitter {
    pub fn from_node(node: Node) -> Self {
        Self {
            events: BoundedNode::from_node_and_capacity(node.create_child("events"), 50),
            _node: node,
        }
    }

    pub fn emit(&mut self, event: Event<'_>) {
        self.events.push(|n| {
            event.write_to_inspect(&n);
            n
        });
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty, Inspector},
    };

    fn assert_emit_inspect_target_version(
        event: Event<'_>,
        event_name: &'static str,
        target_version: &'static str,
    ) {
        let inspector = Inspector::new();
        let mut emitter = Emitter::from_node(inspector.root().create_child("emitter"));

        emitter.emit(event);

        assert_inspect_tree!(
            inspector,
            "root": {
                "emitter": {
                    "events": contains {
                        "capacity": 50u64,
                        "children": {
                            "0": {
                                "event": event_name,
                                "ts": AnyProperty,
                                "target-version": target_version,
                            }
                        }
                    }
                }
            }
        )
    }

    fn assert_emit_inspect_no_target_version(event: Event<'_>, event_name: &'static str) {
        let inspector = Inspector::new();
        let mut emitter = Emitter::from_node(inspector.root().create_child("emitter"));

        emitter.emit(event);

        assert_inspect_tree!(
            inspector,
            "root": {
                "emitter": {
                    "events": contains {
                        "capacity": 50u64,
                        "children": {
                            "0": {
                                "event": event_name,
                                "ts": AnyProperty,
                            }
                        }
                    }
                }
            }
        )
    }

    #[test]
    fn error_checking_for_update() {
        assert_emit_inspect_no_target_version(
            Event::ErrorCheckingForUpdate,
            "ErrorCheckingForUpdate",
        );
    }

    #[test]
    fn no_update_available() {
        assert_emit_inspect_no_target_version(Event::NoUpdateAvailable, "NoUpdateAvailable");
    }

    #[test]
    fn installation_deferred_by_policy() {
        assert_emit_inspect_target_version(
            Event::InstallationDeferredByPolicy { target_version: "some-ver" },
            "InstallationDeferredByPolicy",
            "some-ver",
        );
    }

    #[test]
    fn installation_error() {
        assert_emit_inspect_target_version(
            Event::InstallationError { target_version: "some-ver" },
            "InstallationError",
            "some-ver",
        );
    }

    #[test]
    fn waiting_for_reboot() {
        assert_emit_inspect_target_version(
            Event::WaitingForReboot { target_version: "some-ver" },
            "WaitingForReboot",
            "some-ver",
        );
    }
}
