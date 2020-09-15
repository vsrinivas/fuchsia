// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::events::{self, DaemonEvent};
use anyhow::Result;
use std::time::Duration;

pub trait TargetFinder: Sized {
    fn new(config: &TargetFinderConfig) -> Result<Self>;

    /// The target finder should set up its threads using clones of the sender
    /// end of the channel,
    fn start(&mut self, e: events::Queue<DaemonEvent>) -> Result<()>;
}

#[derive(Copy, Debug, Clone, Eq, PartialEq, Hash)]
pub struct TargetFinderConfig {
    pub interface_discovery_interval: Duration,
    pub broadcast_interval: Duration,
    pub mdns_ttl: u32,
}
