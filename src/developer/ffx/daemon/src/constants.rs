// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::time::Duration;

pub(crate) const DAEMON: &str = "daemon";

#[cfg(not(test))]
pub(crate) const SOCKET: &str = "/tmp/ascendd";

#[cfg(test)]
pub(crate) const SOCKET: &str = "/tmp/ascendd_for_testing_only";

pub(crate) const MAX_RETRY_COUNT: u32 = 30;

// Delay between retry attempts to find the RCS.
pub const RETRY_DELAY: Duration = Duration::from_millis(200);

// The amount of time when awaiting an event (target up, RCS connect, etc)
// before giving up. This may need to be split up into specific timeouts for
// specific events.
pub const EVENT_TIMEOUT: Duration = Duration::from_secs(10);

// Config keys
pub const SSH_PRIV: &str = "ssh.priv";
pub const SSH_PORT: &str = "ssh.port";
