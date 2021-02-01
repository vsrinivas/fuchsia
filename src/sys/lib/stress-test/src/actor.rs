// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;

pub enum ActorError {
    /// The operation did not occur due to the current state of the instance.
    /// The global operation count should not be incremented.
    ///
    /// For example, an actor that tries to delete files from a filesystem may return DoNotCount
    /// if there are no files to delete.
    DoNotCount,

    /// The operation did not occur because the actor requires the environment to be reset.
    /// The global operation count should not be incremented.
    ///
    /// For example, if an actor's connection to a filesystem is severed, they may return
    /// ResetEnvironment to establish a new connection.
    ResetEnvironment,
}

/// Describes the actor and how it should be run in the test.
pub struct ActorConfig<'a> {
    // The name of this actor
    pub name: String,

    // A mutable reference to the actor for this configuration
    pub actor: &'a mut dyn Actor,

    // The number of seconds to wait between actor operations
    pub delay: u64,
}

impl<'a> ActorConfig<'a> {
    pub fn new(name: &str, actor: &'a mut dyn Actor, delay: u64) -> Self {
        Self { name: name.to_string(), actor, delay }
    }
}

#[async_trait]
pub trait Actor: Send + Sync {
    // ActorRunner invokes this function, instructing the actor
    // to perform exactly one operation and return result.
    async fn perform(&mut self) -> Result<(), ActorError>;
}
