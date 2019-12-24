// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::base::{SettingRequest, SettingRequestResponder, SettingType};
use anyhow::Error;

use futures::channel::mpsc::UnboundedSender;
pub type Notifier = UnboundedSender<SettingType>;

/// An command represents messaging from the registry to take a
/// particular action.
pub enum Command {
    ChangeState(State),
    HandleRequest(SettingRequest, SettingRequestResponder),
}

/// A given state the registry entity can move into.
pub enum State {
    Listen(Notifier),
    EndListen,
}

/// The conductor of lifecycle and activity over a number of registered
/// entities.
pub trait Registry {
    fn register(
        &mut self,
        setting_type: SettingType,
        command_sender: UnboundedSender<Command>,
    ) -> Result<(), Error>;
}
