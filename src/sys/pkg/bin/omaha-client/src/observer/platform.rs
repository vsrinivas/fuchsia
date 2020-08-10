// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bounded_node::BoundedNode;
use fuchsia_inspect::Node;
use fuchsia_zircon::{ClockId::Monotonic, Time};

#[derive(Debug)]
pub enum Event<'a> {
    CheckingForUpdates,
    ErrorCheckingForUpdate,
    NoUpdateAvailable,
    InstallationDeferredByPolicy { target_version: Option<&'a str> },
    InstallingUpdate { target_version: Option<&'a str> },
    InstallationError { target_version: Option<&'a str> },
    WaitingForReboot { target_version: Option<&'a str> },
}

impl Event<'_> {
    fn write_to_inspect(&self, node: &Node, session_id: u64) {
        node.record_int("ts", Time::get(Monotonic).into_nanos());
        node.record_uint("session-id", session_id);
        match self {
            Self::CheckingForUpdates => {
                node.record_string("event", "CheckingForUpdates");
            }
            Self::ErrorCheckingForUpdate => {
                node.record_string("event", "ErrorCheckingForUpdate");
            }
            Self::NoUpdateAvailable => {
                node.record_string("event", "NoUpdateAvailable");
            }
            Self::InstallationDeferredByPolicy { target_version } => {
                node.record_string("event", "InstallationDeferredByPolicy");
                node.record_string("target-version", target_version.unwrap_or(""));
            }
            Self::InstallingUpdate { target_version } => {
                node.record_string("event", "InstallingUpdate");
                node.record_string("target-version", target_version.unwrap_or(""));
            }
            Self::InstallationError { target_version } => {
                node.record_string("event", "InstallationError");
                node.record_string("target-version", target_version.unwrap_or(""));
            }
            Self::WaitingForReboot { target_version } => {
                node.record_string("event", "WaitingForReboot");
                node.record_string("target-version", target_version.unwrap_or(""));
            }
        }
    }
}

#[derive(Debug)]
pub struct Emitter {
    events: BoundedNode<Node>,
    // Used to group events from the same update check. Independent from the Omaha session id.
    session_id: u64,
    _node: Node,
}

impl Emitter {
    pub fn from_node(node: Node) -> Self {
        Self {
            events: BoundedNode::from_node_and_capacity(node.create_child("events"), 50),
            session_id: 0,
            _node: node,
        }
    }

    pub fn emit(&mut self, event: Event<'_>) {
        if let Event::CheckingForUpdates = event {
            self.session_id = make_session_id();
        }
        let session_id = self.session_id;
        self.events.push(|n| {
            event.write_to_inspect(&n, session_id);
            n
        });
    }
}

#[cfg(not(test))]
fn make_session_id() -> u64 {
    rand::random()
}

#[cfg(test)]
use mock::make_session_id;

#[cfg(test)]
mod mock {
    use std::cell::RefCell;

    thread_local!(static MOCK_SESSION_ID: RefCell<u64> = RefCell::new(0));

    pub fn make_session_id() -> u64 {
        MOCK_SESSION_ID.with(|id| *id.borrow())
    }

    pub fn set_session_id(new_id: u64) {
        MOCK_SESSION_ID.with(|id| *id.borrow_mut() = new_id);
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fuchsia_inspect::{
            assert_inspect_tree,
            testing::{AnyProperty, TreeAssertion},
            tree_assertion, Inspector,
        },
    };

    static TARGET_VERSION: &'static str = "some-ver";

    fn assert_emit_inspect(event: Event<'_>, child: TreeAssertion) {
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
                            child,
                        }
                    }
                }
            }
        );
    }

    #[test]
    fn checking_for_updates() {
        mock::set_session_id(9);
        assert_emit_inspect(
            Event::CheckingForUpdates,
            tree_assertion!(
                "0": {
                    "event": "CheckingForUpdates",
                    "ts": AnyProperty,
                    "session-id": 9u64
                }
            ),
        )
    }

    #[test]
    fn error_checking_for_update() {
        mock::set_session_id(9);
        assert_emit_inspect(
            Event::ErrorCheckingForUpdate,
            tree_assertion!(
                "0": {
                    "event": "ErrorCheckingForUpdate",
                    "ts": AnyProperty,
                    "session-id": 0u64
                }
            ),
        );
    }

    #[test]
    fn no_update_available() {
        mock::set_session_id(9);
        assert_emit_inspect(
            Event::NoUpdateAvailable,
            tree_assertion!(
                "0": {
                    "event": "NoUpdateAvailable",
                    "ts": AnyProperty,
                    "session-id": 0u64
                }
            ),
        )
    }

    #[test]
    fn installation_deferred_by_policy() {
        mock::set_session_id(9);
        assert_emit_inspect(
            Event::InstallationDeferredByPolicy { target_version: Some(TARGET_VERSION) },
            tree_assertion!(
                "0": {
                    "event": "InstallationDeferredByPolicy",
                    "ts": AnyProperty,
                    "session-id": 0u64,
                    "target-version": TARGET_VERSION,
                }
            ),
        );
    }

    #[test]
    fn installing_update() {
        mock::set_session_id(9);
        assert_emit_inspect(
            Event::InstallingUpdate { target_version: Some(TARGET_VERSION) },
            tree_assertion!(
                "0": {
                    "event": "InstallingUpdate",
                    "ts": AnyProperty,
                    "session-id": 0u64,
                    "target-version": TARGET_VERSION,
                }
            ),
        );
    }

    #[test]
    fn installation_error() {
        mock::set_session_id(9);
        assert_emit_inspect(
            Event::InstallationError { target_version: Some(TARGET_VERSION) },
            tree_assertion!(
                "0": {
                    "event": "InstallationError",
                    "ts": AnyProperty,
                    "session-id": 0u64,
                    "target-version": TARGET_VERSION,
                }
            ),
        );
    }

    #[test]
    fn waiting_for_reboot() {
        mock::set_session_id(9);
        assert_emit_inspect(
            Event::WaitingForReboot { target_version: Some(TARGET_VERSION) },
            tree_assertion!(
                "0": {
                    "event": "WaitingForReboot",
                    "ts": AnyProperty,
                    "session-id": 0u64,
                    "target-version": TARGET_VERSION,
                }
            ),
        );
    }

    #[test]
    fn target_version_defaults_to_empty_string() {
        mock::set_session_id(9);
        assert_emit_inspect(
            Event::InstallationDeferredByPolicy { target_version: None },
            tree_assertion!(
                "0": {
                    "event": "InstallationDeferredByPolicy",
                    "ts": AnyProperty,
                    "session-id": 0u64,
                    "target-version": "",
                }
            ),
        );
    }

    #[test]
    fn session_id_persists() {
        let inspector = Inspector::new();
        let mut emitter = Emitter::from_node(inspector.root().create_child("emitter"));

        mock::set_session_id(9);
        emitter.emit(Event::CheckingForUpdates);
        mock::set_session_id(10);
        emitter.emit(Event::ErrorCheckingForUpdate);

        assert_inspect_tree!(
            inspector,
            "root": {
                "emitter": {
                    "events": contains {
                        "capacity": 50u64,
                        "children": {
                            "0": {
                                "event": "CheckingForUpdates",
                                "ts": AnyProperty,
                                "session-id": 9u64,
                            },
                            "1": {
                                "event": "ErrorCheckingForUpdate",
                                "ts": AnyProperty,
                                "session-id": 9u64,
                            }
                        }
                    }
                }
            }
        )
    }

    #[test]
    fn new_session_new_id() {
        let inspector = Inspector::new();
        let mut emitter = Emitter::from_node(inspector.root().create_child("emitter"));

        mock::set_session_id(9);
        emitter.emit(Event::CheckingForUpdates);
        mock::set_session_id(10);
        emitter.emit(Event::CheckingForUpdates);

        assert_inspect_tree!(
            inspector,
            "root": {
                "emitter": {
                    "events": contains {
                        "capacity": 50u64,
                        "children": {
                            "0": {
                                "event": "CheckingForUpdates",
                                "ts": AnyProperty,
                                "session-id": 9u64,
                            },
                            "1": {
                                "event": "CheckingForUpdates",
                                "ts": AnyProperty,
                                "session-id": 10u64,
                            }
                        }
                    }
                }
            }
        )
    }
}
