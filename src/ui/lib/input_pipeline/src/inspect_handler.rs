// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_device::{Handled, InputDeviceEvent, InputEvent};
use crate::input_handler::InputHandler;
use async_trait::async_trait;
use fuchsia_inspect::{self as inspect, NumericProperty, Property};
use fuchsia_zircon as zx;
use std::collections::HashMap;
use std::rc::Rc;

#[derive(Debug, Hash, PartialEq, Eq)]
enum EventType {
    Keyboard,
    LightSensor,
    ConsumerControls,
    Mouse,
    TouchScreen,
    Touchpad,
    MouseConfig,
    #[cfg(test)]
    Fake,
}

impl std::fmt::Display for EventType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &*self {
            EventType::Keyboard => write!(f, "keyboard"),
            EventType::LightSensor => write!(f, "light_sensor"),
            EventType::ConsumerControls => write!(f, "consumer_controls"),
            EventType::Mouse => write!(f, "mouse"),
            EventType::TouchScreen => write!(f, "touch_screen"),
            EventType::Touchpad => write!(f, "touchpad"),
            EventType::MouseConfig => write!(f, "mouse_config"),
            #[cfg(test)]
            EventType::Fake => write!(f, "fake"),
        }
    }
}

impl EventType {
    /// Creates an `EventType` based on an [InputDeviceEvent].
    pub fn for_device_event(event: &InputDeviceEvent) -> Self {
        match event {
            InputDeviceEvent::Keyboard(_) => EventType::Keyboard,
            InputDeviceEvent::LightSensor(_) => EventType::LightSensor,
            InputDeviceEvent::ConsumerControls(_) => EventType::ConsumerControls,
            InputDeviceEvent::Mouse(_) => EventType::Mouse,
            InputDeviceEvent::TouchScreen(_) => EventType::TouchScreen,
            InputDeviceEvent::Touchpad(_) => EventType::Touchpad,
            InputDeviceEvent::MouseConfig(_) => EventType::MouseConfig,
            #[cfg(test)]
            InputDeviceEvent::Fake => EventType::Fake,
        }
    }
}

#[derive(Debug)]
struct EventCounters {
    /// A node that contains the counters below.
    _node: inspect::Node,
    /// The number of total events that this handler has seen so far.
    events_count: inspect::UintProperty,
    /// The number of total handled events that this handler has seen so far.
    handled_events_count: inspect::UintProperty,
    /// The timestamp (in nanoseconds) when the last event was seen by this
    /// handler (not when the event itself was generated). 0 if unset.
    last_seen_timestamp_ns: inspect::IntProperty,
    /// The event time at which the last recorded event was generated.
    /// 0 if unset.
    last_generated_timestamp_ns: inspect::IntProperty,
}

impl EventCounters {
    fn add_new_into(
        map: &mut HashMap<EventType, EventCounters>,
        root: &inspect::Node,
        event_type: EventType,
    ) {
        let node = root.create_child(format!("{}", event_type));
        let events_count = node.create_uint("events_count", 0);
        let handled_events_count = node.create_uint("handled_events_count", 0);
        let last_seen_timestamp_ns = node.create_int("last_seen_timestamp_ns", 0);
        let last_generated_timestamp_ns = node.create_int("last_generated_timestamp_ns", 0);
        let new_counters = EventCounters {
            _node: node,
            events_count,
            handled_events_count,
            last_seen_timestamp_ns,
            last_generated_timestamp_ns,
        };
        map.insert(event_type, new_counters);
    }

    pub fn count_event(&self, time: zx::Time, event_time: zx::Time, handled: &Handled) {
        self.events_count.add(1);
        if *handled == Handled::Yes {
            self.handled_events_count.add(1);
        }
        self.last_seen_timestamp_ns.set(time.into_nanos());
        self.last_generated_timestamp_ns.set(event_time.into_nanos());
    }
}

/// A [InputHandler] that records various metrics about the flow of events.
/// All events are passed through unmodified.  Some properties of those events
/// may be exposed in the metrics.  No PII information should ever be exposed
/// this way.
#[derive(Debug)]
pub struct InspectHandler {
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
    last_generated_timestamp_ns: inspect::IntProperty,

    /// An inventory of event counters by type.
    events_by_type: HashMap<EventType, EventCounters>,
}

#[async_trait(?Send)]
impl InputHandler for InspectHandler {
    async fn handle_input_event(self: Rc<Self>, input_event: InputEvent) -> Vec<InputEvent> {
        let event_time = input_event.event_time;
        let now = (self.now)();
        self.events_count.add(1);
        self.last_seen_timestamp_ns.set(now.into_nanos());
        self.last_generated_timestamp_ns.set(event_time.into_nanos());
        let event_type = EventType::for_device_event(&input_event.device_event);
        self.events_by_type
            .get(&event_type)
            .unwrap_or_else(|| panic!("no event counters for {}", event_type))
            .count_event(now, event_time, &input_event.handled);
        vec![input_event]
    }
}

impl InspectHandler {
    /// Creates a new inspect handler instance.
    ///
    /// `node` is the inspect node that will receive the stats.
    pub fn new(node: inspect::Node) -> Rc<Self> {
        Self::new_with_now(node, zx::Time::get_monotonic)
    }

    /// Creates a new inspect handler instance, using `now` to supply the current timestamp.
    /// Expected to be useful in testing mainly.
    fn new_with_now(node: inspect::Node, now: fn() -> zx::Time) -> Rc<Self> {
        let event_count = node.create_uint("events_count", 0);
        let last_seen_timestamp_ns = node.create_int("last_seen_timestamp_ns", 0);
        let last_generated_timestamp_ns = node.create_int("last_generated_timestamp_ns", 0);

        let mut events_by_type = HashMap::new();
        EventCounters::add_new_into(&mut events_by_type, &node, EventType::Keyboard);
        EventCounters::add_new_into(&mut events_by_type, &node, EventType::ConsumerControls);
        EventCounters::add_new_into(&mut events_by_type, &node, EventType::Mouse);
        EventCounters::add_new_into(&mut events_by_type, &node, EventType::TouchScreen);
        EventCounters::add_new_into(&mut events_by_type, &node, EventType::Touchpad);
        EventCounters::add_new_into(&mut events_by_type, &node, EventType::MouseConfig);
        #[cfg(test)]
        EventCounters::add_new_into(&mut events_by_type, &node, EventType::Fake);

        Rc::new(Self {
            now,
            _node: node,
            events_count: event_count,
            last_seen_timestamp_ns,
            last_generated_timestamp_ns,
            events_by_type,
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

        let handler = super::InspectHandler::new_with_now(test_node, fixed_now);
        assert_data_tree!(inspector, root: {
            test_node: {
                events_count: 0u64,
                last_seen_timestamp_ns: 0i64,
                last_generated_timestamp_ns: 0i64,
                consumer_controls: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                fake: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                keyboard: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                mouse: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                mouse_config: {
                    events_count: 0u64,
                    handled_events_count: 0u64,
                    last_generated_timestamp_ns: 0i64,
                    last_seen_timestamp_ns: 0i64,
                },
                touch_screen: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                touchpad: {
                    events_count: 0u64,
                    handled_events_count: 0u64,
                    last_generated_timestamp_ns: 0i64,
                    last_seen_timestamp_ns: 0i64,
               },
           }
        });

        handler
            .clone()
            .handle_input_event(testing_utilities::create_fake_input_event(zx::Time::from_nanos(
                43i64,
            )))
            .await;
        assert_data_tree!(inspector, root: {
            test_node: {
                events_count: 1u64,
                last_seen_timestamp_ns: 42i64,
                last_generated_timestamp_ns: 43i64,
                consumer_controls: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                fake: {
                     events_count: 1u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 43i64,
                     last_seen_timestamp_ns: 42i64,
                },
                keyboard: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                mouse: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                mouse_config: {
                    events_count: 0u64,
                    handled_events_count: 0u64,
                    last_generated_timestamp_ns: 0i64,
                    last_seen_timestamp_ns: 0i64,
                },
                touch_screen: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                touchpad: {
                    events_count: 0u64,
                    handled_events_count: 0u64,
                    last_generated_timestamp_ns: 0i64,
                    last_seen_timestamp_ns: 0i64,
               },
            }
        });

        handler
            .clone()
            .handle_input_event(testing_utilities::create_fake_input_event(zx::Time::from_nanos(
                44i64,
            )))
            .await;
        assert_data_tree!(inspector, root: {
            test_node: {
                events_count: 2u64,
                last_seen_timestamp_ns: 42i64,
                last_generated_timestamp_ns: 44i64,
                consumer_controls: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                fake: {
                     events_count: 2u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 44i64,
                     last_seen_timestamp_ns: 42i64,
                },
                keyboard: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                mouse: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                mouse_config: {
                    events_count: 0u64,
                    handled_events_count: 0u64,
                    last_generated_timestamp_ns: 0i64,
                    last_seen_timestamp_ns: 0i64,
                },
                touch_screen: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                touchpad: {
                    events_count: 0u64,
                    handled_events_count: 0u64,
                    last_generated_timestamp_ns: 0i64,
                    last_seen_timestamp_ns: 0i64,
               },
            }
        });

        handler
            .clone()
            .handle_input_event(testing_utilities::create_fake_handled_input_event(
                zx::Time::from_nanos(44),
            ))
            .await;
        assert_data_tree!(inspector, root: {
            test_node: {
                events_count: 3u64,
                last_seen_timestamp_ns: 42i64,
                last_generated_timestamp_ns: 44i64,
                consumer_controls: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                fake: {
                     events_count: 3u64,
                     handled_events_count: 1u64,
                     last_generated_timestamp_ns: 44i64,
                     last_seen_timestamp_ns: 42i64,
                },
                keyboard: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                mouse: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                mouse_config: {
                    events_count: 0u64,
                    handled_events_count: 0u64,
                    last_generated_timestamp_ns: 0i64,
                    last_seen_timestamp_ns: 0i64,
                },
                touch_screen: {
                     events_count: 0u64,
                     handled_events_count: 0u64,
                     last_generated_timestamp_ns: 0i64,
                     last_seen_timestamp_ns: 0i64,
                },
                touchpad: {
                    events_count: 0u64,
                    handled_events_count: 0u64,
                    last_generated_timestamp_ns: 0i64,
                    last_seen_timestamp_ns: 0i64,
               },
            }
        });
    }
}
