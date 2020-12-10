// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! MessageHub for resource monitoring communication.
//!
//! # Summary
//!
//! The monitor MessageHub fosters communication between the resources watchdog
//! and individual monitors. This is a two-way communication path where the
//! watchdog issues commands to the monitors and the monitors return data
//! as it becomes available.
//!
//! Since the watchdog and monitors are brought up independently of each other,
//! no address space is required. A single command to initiate monitoring is
//! available to the watchdog. The watchdog broadcasts this command, reaching
//! all monitors, and then listens on the resulting receptor for responses.
//!
//!
//! # Example
//!
//! ```no_run
//! #use crate::internal::monitor;
//! #use crate::message::base::{Audience, MessengerType};
//!
//! async fn message_example() {
//!     // Create monitor message hub.
//!     let messenger_factory = monitor::message::create_hub();
//!
//!     // Create a messenger for watchdog.
//!     let (watchdog_client, _) = messenger_factory
//!         .create(MessengerType::Unbound)
//!         .await
//!         .expect("should be able to create messenger");
//!
//!     // Create a messenger for monitor.
//!     let (_, mut monitor_receptor) = messenger_factory
//!         .create(MessengerType::Unbound)
//!         .await
//!         .expect("should be able to create messenger");
//!
//!     // Issue monitor command.
//!     let mut data_receptor =
//!     watchdog_client.message(monitor::Payload::Monitor, Audience::Broadcast).send();
//!
//!     let data_payload = monitor::Payload::Data(
//!             monitor::DataBuilder::new(monitor::State::Warning).build());
//!
//!     // Receive command.
//!     if let Ok((monitor::Payload::Monitor, client)) = monitor_receptor.next_payload().await {
//!         client.reply(data_payload.clone()).send().ack();
//!     }
//! }
//! ```
use crate::message_hub_definition;
use fuchsia_zircon as zx;

/// The commands and responses sent between the watchdog and monitors.
#[allow(dead_code)]
#[derive(Clone, Debug, PartialEq)]
pub enum Payload {
    /// Broadcasted to inform monitors to start tracking resources.
    Monitor,
    /// Details captured by a monitor, sent as a response to Monitor.
    Data(Data),
}

/// `State` defines buckets for operating ranges that resources can be observed
/// in. These are coarsely defined levels whose cut-off points are dependent on
/// the resource context. The watchdog will use these signals to indicate what
/// state the service is in.
#[allow(dead_code)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum State {
    /// Resource utilization is or has returned to normal.
    Normal,
    /// Resource utilization has risen or fallen to a warning state.
    Warning,
    /// Resource utilization has reached critical usage.
    Critical,
}

/// `DataBuilder` is a helper for constructing [`Data`] to send through the
/// MessageHub.
pub struct DataBuilder {
    /// Time which data was updated.
    update_time: zx::Time,
    /// The current state of the resource.
    state: State,
    /// Optional extra data describing the state.
    details: Option<Details>,
}

impl DataBuilder {
    /// By default, the builder creates Data with the current time and no
    /// details
    #[allow(dead_code)]
    pub fn new(state: State) -> Self {
        Self { update_time: zx::Time::get_monotonic(), state, details: None }
    }

    #[allow(dead_code)]
    pub fn set_details(mut self, details: Details) -> Self {
        self.details = Some(details);
        self
    }

    #[allow(dead_code)]
    pub fn build(self) -> Data {
        Data { update_time: self.update_time, state: self.state, details: self.details }
    }
}

/// `Data` captures the resource context at any given time observed by a
/// monitor.
#[derive(Clone, Debug, PartialEq)]
pub struct Data {
    /// Time which data was updated.
    update_time: zx::Time,
    /// The current state of the resource.
    state: State,
    /// Optional extra data describing the state.
    details: Option<Details>,
}

/// `Details` defines the types of information that can be returned with
/// [`Data`] to provide additional details surrounding an observed [`State`].
/// This enum will become populated as monitors are implemented.
#[derive(Clone, Debug, PartialEq)]
pub enum Details {}

message_hub_definition!(Payload);

#[cfg(test)]
mod tests {
    use crate::internal::monitor;
    use crate::message::base::{Audience, MessengerType};

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_messaging() {
        // Create monitor message hub.
        let messenger_factory = monitor::message::create_hub();

        // Create a messenger for watchdog.
        let (watchdog_client, _) = messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("should be able to create messenger");

        // Create a messenger for monitor.
        let (_, mut monitor_receptor) = messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("should be able to create messenger");

        // Issue monitor command.
        let mut data_receptor =
            watchdog_client.message(monitor::Payload::Monitor, Audience::Broadcast).send();

        let data_payload =
            monitor::Payload::Data(monitor::DataBuilder::new(monitor::State::Warning).build());

        // Receive command.
        if let Ok((monitor::Payload::Monitor, client)) = monitor_receptor.next_payload().await {
            client.reply(data_payload.clone()).send().ack();
        } else {
            panic!("expected payload");
        }

        // Ensure the payload was received by the messenger that broadcasted
        // the Monitor command.
        assert_eq!(data_receptor.next_payload().await.expect("should have data").0, data_payload);
    }
}
