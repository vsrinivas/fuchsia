// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::event;
use crate::message::base::{role, Audience};
use crate::payload_convert;
use crate::service;
use std::sync::Arc;

#[derive(Clone, Debug, PartialEq)]
pub enum Payload {
    Event(Event),
}

/// Top level definition of events in the setting service. A new type should
/// be defined for each component (setting, agent, proxy, etc.)
#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    Custom(&'static str),
    CameraUpdate(camera_watcher::Event),
    Earcon(earcon::Event),
    MediaButtons(media_buttons::Event),
    Restore(restore::Event),
    Closed(&'static str),
    Handler(SettingType, handler::Event),
}

#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Address {
    SettingProxy(SettingType),
}

#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Role {
    Sink,
}

pub mod camera_watcher {
    #[derive(PartialEq, Clone, Debug, Eq, Hash)]
    pub enum Event {
        // Indicates that the camera's software mute state changed.
        OnSWMuteState(bool),
    }

    impl From<bool> for Event {
        fn from(muted: bool) -> Self {
            Self::OnSWMuteState(muted)
        }
    }
}

pub mod earcon {
    #[derive(PartialEq, Clone, Debug, Eq, Hash)]
    pub enum Event {
        // Indicates the specified earcon type was not played when it normally
        // would.
        Suppressed(EarconType),
    }

    #[derive(PartialEq, Clone, Debug, Eq, Hash)]
    pub enum EarconType {
        Volume,
    }
}

pub mod handler {
    use crate::handler::base::Request;
    use crate::handler::setting_handler::ExitResult;

    #[derive(PartialEq, Clone, Debug)]
    pub enum Event {
        Exit(ExitResult),
        Request(Action, Request),
        Teardown,
    }

    /// Possible actions taken on a `Request` by a Setting Handler.
    #[derive(PartialEq, Clone, Debug)]
    pub enum Action {
        Execute,
        Retry,
        Timeout,
        AttemptsExceeded,
    }
}

pub mod media_buttons {
    use crate::input::{ButtonType, VolumeGain};

    #[derive(PartialEq, Clone, Debug)]
    pub enum Event {
        OnButton(ButtonType),
        OnVolume(VolumeGain),
    }

    impl From<ButtonType> for Event {
        fn from(button_type: ButtonType) -> Self {
            Self::OnButton(button_type)
        }
    }

    impl From<VolumeGain> for Event {
        fn from(volume_gain: VolumeGain) -> Self {
            Self::OnVolume(volume_gain)
        }
    }
}

pub mod restore {
    use crate::base::SettingType;

    #[derive(PartialEq, Clone, Debug, Eq, Hash)]
    pub enum Event {
        // Indicates that the setting type does nothing for a call to restore.
        NoOp(SettingType),
    }
}

payload_convert!(Event, Payload);

/// Publisher is a helper for producing logs. It simplifies message creation for
/// each event and associates an address with these messages at construction.
#[derive(Clone, Debug)]
pub struct Publisher {
    messenger: service::message::Messenger,
}

impl Publisher {
    pub async fn create(
        factory: &service::message::Factory,
        messenger_type: service::message::MessengerType,
    ) -> Publisher {
        let (messenger, _) = factory
            .create(messenger_type)
            .await
            .expect("should be able to retrieve messenger for publisher");

        Publisher { messenger }
    }

    /// Broadcasts event to the message hub.
    pub fn send_event(&self, event: Event) {
        self.messenger
            .message(
                Payload::Event(event).into(),
                Audience::Role(role::Signature::role(service::Role::Event(event::Role::Sink))),
            )
            .send()
            .ack();
    }
}

pub mod subscriber {
    use super::*;
    use futures::future::BoxFuture;

    pub type BlueprintHandle = Arc<dyn Blueprint>;

    /// The Subscriber Blueprint is used for spawning new subscribers on demand
    /// in the environment.
    pub trait Blueprint {
        fn create(&self, message_factory: service::message::Factory) -> BoxFuture<'static, ()>;
    }
}
