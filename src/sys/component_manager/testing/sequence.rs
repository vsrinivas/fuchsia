// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        events::{event_name, EventDescriptor, EventSource, EventStream},
        matcher::EventMatcher,
    },
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    futures::{channel::oneshot, future::BoxFuture},
    std::convert::TryFrom,
};

/// Ordering is used by `EventSequence::subscribe_and_expect`to determine if it should allow events to be
/// verified in any order or only in the order specified by the test.
#[derive(Clone)]
pub enum Ordering {
    Ordered,
    Unordered,
}

#[derive(Clone)]
pub struct EventSequence {
    groups: Vec<EventGroup>,
}

impl EventSequence {
    pub fn new() -> Self {
        Self { groups: vec![] }
    }

    pub fn then(self, matcher: EventMatcher) -> Self {
        self.all_of(vec![matcher], Ordering::Ordered)
    }

    pub fn all_of(mut self, events: Vec<EventMatcher>, ordering: Ordering) -> Self {
        self.groups.push(EventGroup::new(events, ordering));
        self
    }

    /// Verify that the events in this sequence are received from the provided
    /// EventStream.
    pub async fn expect(mut self, mut event_stream: EventStream) -> Result<(), Error> {
        while !self.groups.is_empty() {
            match event_stream.next().await {
                Err(e) => return Err(e.into()),
                Ok(event) => {
                    let actual_event = EventDescriptor::try_from(&event)?;
                    if actual_event.target_moniker != Some(".".to_string()) {
                        let _ = self.next(&actual_event)?;
                    }
                }
            }
        }
        Ok(())
    }

    /// Verifies that the given events are received from the event system. Based on the vector of
    /// events passed in this function will subscribe to an event stream with the relevant event
    /// types and verify that the correct events for the component are received.
    pub async fn subscribe_and_expect<'a>(
        self,
        event_source: &mut EventSource,
    ) -> Result<BoxFuture<'a, Result<(), Error>>, Error> {
        let event_names = self.event_names()?;
        let event_stream = event_source.subscribe(event_names).await?;
        let expected_events = self.clone();
        let (tx, rx) = oneshot::channel();
        fasync::Task::spawn(async move {
            let res = expected_events.expect(event_stream).await;
            tx.send(res).expect("Unable to send result");
        })
        .detach();

        Ok(Box::pin(async move { rx.await? }))
    }

    pub fn is_empty(&self) -> bool {
        self.groups.is_empty()
    }

    pub fn event_names(&self) -> Result<Vec<String>, Error> {
        let mut event_names = vec![];
        for group in &self.groups {
            let mut group_event_names = group.event_names()?;
            event_names.append(&mut group_event_names);
        }
        event_names.dedup();
        Ok(event_names)
    }

    // Takes in an EventDescriptor and tests it against the first EventGroup.
    // If an EventGroup has been entirely consumed (no further EventMatchers
    // to match against) then the EventGroup is dropped. If the
    // EventSequence have not been entirely consumed, and the incoming
    // EventDescriptor does not match the first sequence then an error is returned.
    // If the EventSequence is empty, then Ok(false) is returned. Otherwise, if
    // there is a positive match, then Ok(true) is returned.
    pub fn next(&mut self, event: &EventDescriptor) -> Result<(), Error> {
        loop {
            if self.groups.is_empty() {
                return Ok(());
            }
            let group = &mut self.groups[0];
            if group.next(event)? {
                if group.is_empty() {
                    self.groups.remove(0);
                }
                return Ok(());
            }
            self.groups.remove(0);
        }
    }
}

#[derive(Clone)]
pub struct EventGroup {
    events: Vec<EventMatcher>,
    ordering: Ordering,
}

impl EventGroup {
    pub fn new(events: Vec<EventMatcher>, ordering: Ordering) -> Self {
        Self { events, ordering }
    }

    pub fn is_empty(&self) -> bool {
        self.events.is_empty()
    }

    pub fn event_names(&self) -> Result<Vec<String>, Error> {
        let mut event_names = vec![];
        for event in &self.events {
            if let Some(event_type) = &event.event_type {
                event_names.push(event_name(&event_type.value()));
            } else {
                return Err(format_err!("No event name or type set for matcher {:?}", event));
            }
        }
        event_names.dedup();
        Ok(event_names)
    }

    // Takes in an EventDescriptor and tests it against the next EventMatcher in
    // the sequence. If the sequence is empty, then Ok(false) is returned. If
    // the sequence is Ordered, then this tests it against the first
    // EventMatcher. If the sequence is Unordered, then this tests the
    // EventDescriptor against all EventMatchers in the sequence to find a
    // match. In either case, if no match is found, then an Error is returned.
    // If there is a positive match, then Ok(true) is returned.
    pub fn next(&mut self, event: &EventDescriptor) -> Result<bool, Error> {
        if self.events.is_empty() {
            return Ok(false);
        }
        let expected_event = match self.ordering {
            Ordering::Ordered => self.events.remove(0),
            Ordering::Unordered => {
                if let Some((index, _)) = self
                    .events
                    .iter()
                    .enumerate()
                    .find(|&matcher| matcher.1.matches(&event).is_ok())
                {
                    self.events.remove(index)
                } else {
                    return Err(format_err!("Failed to find event: {:?}", event));
                }
            }
        };
        expected_event.matches(&event).map(|_| true)
    }
}
