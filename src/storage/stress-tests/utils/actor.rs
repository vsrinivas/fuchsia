// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::instance::InstanceUnderTest, async_trait::async_trait};

pub enum ActorError {
    /// The operation did not occur due to the current state of the instance.
    /// The global operation count should not be incremented.
    ///
    /// For example, an actor that tries to delete files from a filesystem may return DoNotCount
    /// if there are no files to delete.
    DoNotCount,

    /// The operation did not occur because the actor requires a new instance.
    /// The global operation count should not be incremented.
    ///
    /// For example, if an actor's connection to a filesystem is severed, they may return
    /// GetNewInstance to establish a new connection.
    GetNewInstance,
}

/// Describes the actor and how it should be run in the test.
pub struct ActorConfig<I: InstanceUnderTest> {
    pub name: String,
    pub actor: Box<dyn Actor<I>>,
    pub delay: u64,
}

impl<I: InstanceUnderTest> ActorConfig<I> {
    pub fn new<A: Actor<I>>(name: &str, actor: A, delay: u64) -> Self {
        Self { name: name.to_string(), actor: Box::new(actor), delay }
    }
}

#[async_trait]
pub trait Actor<I: InstanceUnderTest>: 'static + Send + Sync {
    async fn perform(&mut self, instance: &mut I) -> Result<(), ActorError>;
}
