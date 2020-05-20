// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::io;
use std::time::Duration;

use crate::target::Target;
use futures::channel::mpsc;

pub trait TargetFinder: Sized {
    fn new(config: &TargetFinderConfig) -> io::Result<Self>;

    /// The target finder should set up its threads using clones of the sender
    /// end of the channel,
    fn start(&self, s: &mpsc::UnboundedSender<Target>) -> io::Result<()>;
}

#[derive(Copy, Debug, Clone, Eq, PartialEq, Hash)]
pub struct TargetFinderConfig {
    pub broadcast_interval: Duration,
    pub mdns_ttl: u8,
}
