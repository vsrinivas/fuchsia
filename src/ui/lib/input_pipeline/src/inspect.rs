// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_device;
use crate::input_handler::InputHandler;
use async_trait::async_trait;
use fuchsia_inspect::{self as inspect, NumericProperty, Property};
use fuchsia_zircon as zx;
use std::rc::Rc;

/// A [InputHandler] that records various metrics about the flow of events.
/// All events are passed through unmodified.  Some properties of those events
/// may be exposed in the metrics.  No PII information should ever be exposed
/// this way.
#[derive(Debug)]
pub struct Handler {
    /// A function that obtains the current timestamp.
    now: fn() -> zx::Time,
    /// A node that contains the statistics about this particular handler.
    _node: inspect::Node,
    /// The number of total events that this handler has seen so far.
    events_count: inspect::UintProperty,
    /// The timestamp (in nanoseconds) when the last event was seen by this
    /// handler (not when the event itself was generated). 0 if unset.
    last_seen_timestamp_ns: inspect::IntProperty,
    /// The event time at which the last recorded event was generated.
    /// 0 if unset.
    last_generated_timestamp_ns: inspect::UintProperty,
}

#[async_trait(?Send)]
impl InputHandler for Handler {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        self.events_count.add(1);
        self.last_seen_timestamp_ns.set((self.now)().into_nanos());
        self.last_generated_timestamp_ns.set(input_event.event_time);
        vec![input_event]
    }
}

impl Handler {
    /// Creates a new inspect handler instance.
    ///
    /// `node` is the inspect node that will receive the stats.
    pub fn new(node: inspect::Node) -> Rc<Self> {
        Handler::new_with_now(node, zx::Time::get_monotonic)
    }

    /// Creates a new inspect handler instance, using `now` to supply the current timestamp.
    /// Expected to be useful in testing mainly.
    fn new_with_now(node: inspect::Node, now: fn() -> zx::Time) -> Rc<Self> {
        let event_count = node.create_uint("events_count", 0);
        let last_seen_timestamp_ns = node.create_int("last_seen_timestamp_ns", 0);
        let last_generated_timestamp_ns = node.create_uint("last_generated_timestamp_ns", 0);
        Rc::new(Self {
            now,
            _node: node,
            events_count: event_count,
            last_seen_timestamp_ns,
            last_generated_timestamp_ns,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing_utilities;
    use fuchsia_async as fasync;
    use fuchsia_inspect::assert_data_tree;

    fn fixed_now() -> zx::Time {
        zx::Time::ZERO + zx::Duration::from_nanos(42)
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_inspect() {
        let inspector = inspect::Inspector::new();
        let root = inspector.root();
        let test_node = root.create_child("test_node");

        let handler = super::Handler::new_with_now(test_node, fixed_now);
        assert_data_tree!(inspector, root: {
            test_node: {
                events_count: 0u64,
                last_seen_timestamp_ns: 0i64,
                last_generated_timestamp_ns: 0u64,
            }
        });

        handler.clone().handle_input_event(testing_utilities::create_fake_input_event(43u64)).await;
        assert_data_tree!(inspector, root: {
            test_node: {
                events_count: 1u64,
                last_seen_timestamp_ns: 42i64,
                last_generated_timestamp_ns: 43u64,
            }
        });

        handler.clone().handle_input_event(testing_utilities::create_fake_input_event(44u64)).await;
        assert_data_tree!(inspector, root: {
            test_node: {
                events_count: 2u64,
                last_seen_timestamp_ns: 42i64,
                last_generated_timestamp_ns: 44u64,
            }
        });
    }
}
