// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_setui::*;
use std::sync::mpsc::Sender;

pub type ProcessMutation = dyn Fn(&Mutation) -> Result<Option<SettingData>, Error> + Send + Sync;
pub type BoxedSettingCodec<T> = Box<dyn SettingCodec<T> + Send + Sync>;
pub type BoxedStore = Box<dyn Store + Send + Sync>;

/// Trait defining interface setting service uses to relay operations. Each
/// adapter specifies the setting type it handles, which it will accept
/// mutations for and relay updates.
pub trait Adapter {
    /// Returns the setting type this adapter is responsible for handling.
    fn get_type(&self) -> SettingType;

    /// Applies a mutation on the given adapter.
    fn mutate(&mut self, mutation: &fidl_fuchsia_setui::Mutation) -> MutationResponse;

    /// Registers a listener. The current value known to the client is passed
    /// along. If an updated value is known, the sender is immediately invoked.
    /// Otherwise, the sender is stored for later invocation.
    fn listen(&self, sender: Sender<SettingData>, last_seen_data: Option<&SettingData>);
}

/// Trait for encoding and decoding Settings.
pub trait SettingCodec<T: ToString> {
    fn encode(&self, data: SettingData) -> Result<T, Error>;

    fn decode(&self, encoded: T) -> Result<SettingData, Error>;
}

pub trait Store {
    /// Writes value to presistent storage.
    fn write(&self, data: SettingData) -> Result<(), Error>;

    /// Reads value from persistent storage
    fn read(&self) -> Result<Option<SettingData>, Error>;
}
