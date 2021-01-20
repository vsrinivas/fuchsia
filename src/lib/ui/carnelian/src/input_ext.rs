// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    geometry::{IntPoint, IntSize},
    input::*,
};
use {
    fuchsia_zircon::Time,
    std::collections::{BTreeMap, VecDeque},
};

#[derive(Clone, Copy)]
struct ContactDetails {
    event_time: u64,
    location: IntPoint,
    size: IntSize,
}

struct DownContact {
    last: ContactDetails,
    next: ContactDetails,
}

struct TouchDevice {
    down_contacts: BTreeMap<touch::ContactId, DownContact>,
    buttons: ButtonSet,
}

pub struct TouchEventResampler {
    events: VecDeque<Event>,
    touch_devices: BTreeMap<DeviceId, TouchDevice>,
}

impl TouchEventResampler {
    /// Create new touch event resampler.
    pub fn new() -> Self {
        Self { events: VecDeque::new(), touch_devices: BTreeMap::new() }
    }

    /// Enqueue any type of event.
    pub fn enqueue(&mut self, event: Event) {
        self.events.push_back(event);
    }

    /// Dequeue non-moved events older than |sample_time| and sample touch
    /// contacts at |sample_time|.
    pub fn dequeue_and_sample(&mut self, sample_time: Time) -> Vec<Event> {
        let sample_time_ns = sample_time.into_nanos() as u64;

        // Process events until sample time.
        self.process_events(sample_time_ns);

        // Dequeue events older than sample time.
        let mut events = self.dequeue_events_until(sample_time_ns);

        // Add resampled move events for all tracked touch contacts.
        events.extend(self.touch_devices.iter().filter_map(|(device_id, device)| {
            if device.down_contacts.is_empty() {
                None
            } else {
                let contacts = device.down_contacts.iter().map(|(contact_id, contact)| {
                    let p = contact.last;
                    let n = contact.next;
                    touch::Contact {
                        contact_id: *contact_id,
                        // Resample location and size if the next contact details are passed
                        // sample time. Otherwise, use the latest contact details we have.
                        phase: if n.event_time > sample_time_ns && n.event_time > p.event_time {
                            let interval = (n.event_time - p.event_time) as f32;
                            let scalar = (sample_time_ns - p.event_time) as f32 / interval;
                            let location =
                                p.location.to_f32() + (n.location - p.location).to_f32() * scalar;
                            let size = p.size.to_f32() + (n.size - p.size).to_f32() * scalar;
                            touch::Phase::Moved(location.to_i32(), size.to_i32())
                        } else {
                            touch::Phase::Moved(n.location, n.size)
                        },
                    }
                });

                let touch_event =
                    touch::Event { contacts: contacts.collect(), buttons: device.buttons.clone() };
                Some(Event {
                    event_time: sample_time_ns,
                    device_id: device_id.clone(),
                    event_type: EventType::Touch(touch_event),
                })
            }
        }));

        events
    }

    fn process_events(&mut self, sample_time_ns: u64) {
        for Event { event_time, device_id, event_type } in &self.events {
            if let EventType::Touch(touch_event) = event_type {
                // Update contact details if event time is more recent than sample time. Otherwise,
                // add/remove contact from currently tracked down contacts.
                if *event_time > sample_time_ns {
                    if let Some(device) = self.touch_devices.get_mut(&device_id.clone()) {
                        for touch::Contact { contact_id, phase } in &touch_event.contacts {
                            if let touch::Phase::Moved(location, size) = phase {
                                if let Some(down_contact) =
                                    device.down_contacts.get_mut(&contact_id)
                                {
                                    // Update contact details if current details are not already more
                                    // recent than sample time.
                                    if down_contact.next.event_time < sample_time_ns {
                                        down_contact.next = ContactDetails {
                                            event_time: *event_time,
                                            location: *location,
                                            size: *size,
                                        };
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // Insert device if it doesn't already exist.
                    let device = self.touch_devices.entry(device_id.clone()).or_insert_with(|| {
                        TouchDevice {
                            down_contacts: BTreeMap::new(),
                            buttons: touch_event.buttons.clone(),
                        }
                    });

                    // Update buttons state.
                    device.buttons = touch_event.buttons.clone();

                    for contact in &touch_event.contacts {
                        match contact.phase {
                            touch::Phase::Down(location, size)
                            | touch::Phase::Moved(location, size) => {
                                let details =
                                    ContactDetails { event_time: *event_time, location, size };
                                device.down_contacts.insert(
                                    contact.contact_id,
                                    DownContact { last: details, next: details },
                                );
                            }
                            touch::Phase::Up | touch::Phase::Remove | touch::Phase::Cancel => {
                                device.down_contacts.remove(&contact.contact_id);
                            }
                        }
                    }
                }
            }
        }
    }

    fn dequeue_events_until(&mut self, sample_time_ns: u64) -> Vec<Event> {
        let mut events = vec![];

        while let Some(event) = self.events.front() {
            // Stop dequeuing events if more recent than sample time.
            if event.event_time > sample_time_ns {
                break;
            }

            let event = self.events.pop_front().unwrap();
            if let EventType::Touch(touch_event) = &event.event_type {
                if let Some(device) = self.touch_devices.get(&event.device_id.clone()) {
                    let contacts = touch_event.contacts.iter().filter_map(|contact| {
                        match contact.phase {
                            touch::Phase::Moved(_, _) => {
                                // Skip moved phase if we're tracking contact.
                                if device.down_contacts.contains_key(&contact.contact_id) {
                                    None
                                } else {
                                    Some(contact.clone())
                                }
                            }
                            _ => Some(contact.clone()),
                        }
                    });

                    let touch_event = touch::Event {
                        contacts: contacts.collect(),
                        buttons: device.buttons.clone(),
                    };
                    if !touch_event.contacts.is_empty() {
                        events.push(Event {
                            event_time: event.event_time,
                            device_id: event.device_id.clone(),
                            event_type: EventType::Touch(touch_event),
                        });
                    }
                }
            } else {
                events.push(event);
            }
        }

        events
    }
}

#[cfg(test)]
mod touch_event_resampling_tests {
    use super::*;
    use std::collections::HashSet;

    fn create_test_down_phase(x: i32, y: i32) -> touch::Phase {
        touch::Phase::Down(euclid::point2(x, y), IntSize::zero())
    }

    fn create_test_moved_phase(x: i32, y: i32) -> touch::Phase {
        touch::Phase::Moved(euclid::point2(x, y), IntSize::zero())
    }

    fn create_test_event(phase: touch::Phase, event_time: u64) -> Event {
        let touch_event = touch::Event {
            contacts: vec![touch::Contact { contact_id: touch::ContactId(100), phase }],
            buttons: ButtonSet::new(&HashSet::new()),
        };
        Event {
            event_time: event_time,
            device_id: DeviceId("test-device-id-1".to_string()),
            event_type: EventType::Touch(touch_event),
        }
    }

    #[test]
    fn test_resampling() {
        let mut resampler = TouchEventResampler::new();

        resampler.enqueue(create_test_event(create_test_down_phase(0, 0), 1000));
        resampler.enqueue(create_test_event(create_test_moved_phase(10, 0), 2000));
        resampler.enqueue(create_test_event(create_test_moved_phase(20, 0), 3000));
        resampler.enqueue(create_test_event(create_test_moved_phase(30, 0), 4000));
        resampler.enqueue(create_test_event(touch::Phase::Up, 4000));

        // No events should be dequeue at time 0.
        assert_eq!(resampler.dequeue_and_sample(Time::from_nanos(500)), vec![]);

        // Down event and first resampled moved event.
        let result = resampler.dequeue_and_sample(Time::from_nanos(1500));
        assert_eq!(result.len(), 2);
        assert_eq!(result[0], create_test_event(create_test_down_phase(0, 0), 1000));
        assert_eq!(result[1], create_test_event(create_test_moved_phase(5, 0), 1500));

        // One resampled moved event.
        let result = resampler.dequeue_and_sample(Time::from_nanos(2500));
        assert_eq!(result.len(), 1);
        assert_eq!(result[0], create_test_event(create_test_moved_phase(15, 0), 2500));

        // Another resampled moved event.
        let result = resampler.dequeue_and_sample(Time::from_nanos(3500));
        assert_eq!(result.len(), 1);
        assert_eq!(result[0], create_test_event(create_test_moved_phase(25, 0), 3500));

        // Last resampled moved event.
        let result = resampler.dequeue_and_sample(Time::from_nanos(4500));
        assert_eq!(result.len(), 2);
        assert_eq!(result[0], create_test_event(create_test_moved_phase(30, 0), 4000));
        assert_eq!(result[1], create_test_event(touch::Phase::Up, 4000));
    }
}
