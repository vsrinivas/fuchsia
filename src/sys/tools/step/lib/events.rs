// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    std::sync::{Arc, Mutex},
    test_utils_lib::events::{EventSource, EventStream, Handler},
};

/// Prints detailed description of an `event`
fn print_event_details(index: usize, event: &fsys::Event) {
    println!("------- EVENT {} -------", index);
    let header = event.header.as_ref().unwrap();
    let moniker = header.moniker.as_ref().unwrap();
    let component_url = header.component_url.as_ref().unwrap();
    println!("Timestamp: {}", header.timestamp.unwrap());
    println!("Origin: \"{}\" ({})", moniker, component_url);

    match &event.event_result {
        Some(fsys::EventResult::Payload(payload)) => {
            println!("{:#?}", payload);
        }
        Some(fsys::EventResult::Error(error)) => {
            println!("{:#?}", error);
        }
        _ => {
            println!("Unknown/Missing Payload");
        }
    }
}

/// Prints a one-line summary of an `event`
fn print_event_summary(index: usize, event: &fsys::Event) {
    let header = event.header.as_ref().unwrap();
    let moniker = header.moniker.as_ref().unwrap();
    let event_type = header.event_type.unwrap();
    println!("{}: {:?} ({})", index, event_type, moniker);
}

/// Listens for events matching given event types.
/// Events are stored into a thread-safe list.
pub struct EventListener {
    /// If |true|, events will be automatically resumed
    /// ensuring that component manager does not get blocked.
    auto_resume: bool,

    /// List of event types that this listener is registered to
    event_types: Vec<String>,

    /// List of events captured by this listener
    events: Arc<Mutex<Vec<fsys::Event>>>,
}

impl EventListener {
    pub async fn new(
        auto_resume: bool,
        event_types: Vec<String>,
        event_source: &EventSource,
    ) -> Self {
        let event_types_str: Vec<&str> = event_types.iter().map(|x| x.as_ref()).collect();
        let server_end = event_source.subscribe_endpoint(event_types_str).await.unwrap();
        let events = Arc::new(Mutex::new(vec![]));
        let clone = events.clone();

        // Poll the event stream on a new thread
        fasync::Task::blocking(async move {
            let mut event_stream = EventStream::new(server_end.into_stream().unwrap());
            while let Ok(mut event) = event_stream.next().await {
                if auto_resume {
                    // Take the event handler and drop it.
                    // This causes the event to be resumed.
                    event.handler.take().unwrap();
                }

                let mut locked = clone.lock().unwrap();
                locked.push(event);
            }
        })
        .detach();

        Self { auto_resume, event_types, events }
    }

    pub fn num_events(&self) -> usize {
        let locked = self.events.lock().unwrap();
        locked.len()
    }

    /// Prints all captured events to stdout.
    pub fn print_events(&self, detailed: bool) {
        let locked = self.events.lock().unwrap();

        for (index, event) in locked.iter().enumerate() {
            if detailed {
                print_event_details(index, event);
            } else {
                print_event_summary(index, event);
            }
        }
    }

    /// Resumes an event at index `event_id`
    pub async fn resume(&self, event_id: usize) -> Result<(), Error> {
        if self.auto_resume {
            return Err(anyhow!("This is an auto-resume listener"));
        }

        let event = {
            let mut locked = self.events.lock().unwrap();
            if locked.len() <= event_id {
                return Err(anyhow!("Invalid event id: {}", event_id));
            }
            locked.remove(event_id)
        };

        event.resume().await.unwrap();
        Ok(())
    }
}

impl std::fmt::Display for EventListener {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{:?} [auto_resume:{}] [num_events:{}] ",
            self.event_types,
            self.auto_resume,
            self.num_events()
        )
    }
}
