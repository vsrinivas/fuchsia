// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Resource monitoring communication.
//!
//! # Summary
//!
//! The monitor MessageHub definitions fosters communication between the
//! resources watchdog and individual monitors. This is a two-way communication
//! path where the watchdog issues commands to the monitors and the monitors
//! return data as it becomes available.
//!
//! Since the watchdog and monitors are brought up independently of each other,
//! communication initiation is through broadcasting. A single command to
//! initiate monitoring is available to the watchdog. The watchdog broadcasts
//! this command, reaching all monitors, and then listens on the resulting
//! receptor for responses.
//!
//!
//! # Example
//!
//! ```no_run
//! #use crate::monitor;
//! #use crate::service::base::{Audience, MessengerType};
//!
//! async fn message_example() {
//!     // Create monitor message hub.
//!     let delegate = service::MessageHub::create_hub();
//!
//!     // Create a messenger for watchdog.
//!     let (watchdog_client, _) = delegate
//!         .create(MessengerType::Unbound)
//!         .await
//!         .expect("should be able to create messenger");
//!
//!     // Create a messenger for monitor.
//!     let (_, mut monitor_receptor) = delegate
//!         .messenger_builder(MessengerType::Unbound)
//!         .add_role(role::Signature::role(service::Role::Monitor(monitor::Role::Monitor)))
//!         .build()
//!         .await
//!         .expect("should be able to create messenger");
//!
//!     // Issue monitor command.
//!     let mut data_receptor = watchdog_client
//!         .message(request_payload.clone(),
//!             Audience::Role(role::Signature::role(service::Role::Monitor(monitor::Role::Monitor,
//!             )))).send();
//!
//!     let data_payload = monitor::Payload::Data(
//!             monitor::DataBuilder::new(monitor::State::Warning).build());
//!
//!     // Receive command.
//!     if let Ok((monitor::Payload::Monitor.into(), client)) =
//!             monitor_receptor.next_payload().await {
//!         client.reply(data_payload..into()).send().ack();
//!     }
//! }
//! ```

use crate::clock::now;
use crate::payload_convert;
use fuchsia_zircon as zx;

pub mod base;
pub mod environment;

/// The commands and responses sent between the watchdog and monitors.
#[allow(dead_code)]
#[derive(Clone, Debug, PartialEq)]
pub enum Payload {
    /// Broadcasted to inform monitors to start tracking resources.
    ///
    /// A monitor should reply to this message with updates
    /// about the resource it is monitoring, using the [`Data`] variant as the
    /// payload for those updates.
    Monitor,
    /// Details captured by a monitor, sent as a response to Monitor.
    Data(Data),
}

#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Role {
    Monitor,
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
    pub(crate) fn new(state: State) -> Self {
        Self { update_time: now(), state, details: None }
    }

    #[allow(dead_code)]
    pub(crate) fn set_details(mut self, details: Details) -> Self {
        self.details = Some(details);
        self
    }

    #[allow(dead_code)]
    pub(crate) fn build(self) -> Data {
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

payload_convert!(Monitor, Payload);

#[cfg(test)]
mod tests {
    use crate::message::base::{role, Audience, MessengerType};
    use crate::message::MessageHubUtil;
    use crate::monitor;
    use crate::service;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_messaging() {
        // Create monitor message hub.
        let delegate = service::MessageHub::create_hub();

        // Create a messenger for watchdog.
        let (watchdog_client, _) = delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should be able to create messenger");

        // Create a messenger for monitor.
        let (_, mut monitor_receptor) = delegate
            .messenger_builder(MessengerType::Unbound)
            .add_role(role::Signature::role(service::Role::Monitor(monitor::Role::Monitor)))
            .build()
            .await
            .expect("should be able to create messenger");

        let request_payload: service::Payload = monitor::Payload::Monitor.into();

        // Issue monitor command.
        let mut data_receptor = watchdog_client
            .message(
                request_payload.clone(),
                Audience::Role(role::Signature::role(service::Role::Monitor(
                    monitor::Role::Monitor,
                ))),
            )
            .send();

        let data_payload: service::Payload =
            monitor::Payload::Data(monitor::DataBuilder::new(monitor::State::Warning).build())
                .into();

        // Receive command.
        if let Ok((payload, client)) = monitor_receptor.next_payload().await {
            assert_eq!(payload, request_payload);
            client.reply(data_payload.clone()).send().ack();
        } else {
            panic!("expected payload");
        }

        // Ensure the payload was received by the messenger that broadcasted
        // the Monitor command.
        assert_eq!(data_receptor.next_payload().await.expect("should have data").0, data_payload);
    }
}
