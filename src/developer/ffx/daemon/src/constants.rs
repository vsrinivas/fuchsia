// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::time::Duration;

pub(crate) const DAEMON: &str = "daemon";

#[cfg(not(test))]
pub(crate) const DEFAULT_SOCKET: &str = "/tmp/ascendd";

#[cfg(test)]
pub(crate) const DEFAULT_SOCKET: &str = "/tmp/ascendd_for_testing_only";

pub(crate) const DEFAULT_MAX_RETRY_COUNT: u64 = 30;

// Delay between retry attempts to find the RCS.
pub(crate) const RETRY_DELAY: Duration = Duration::from_millis(200);

// The amount of time when awaiting an event (target up, RCS connect, etc)
// before giving up. This may need to be split up into specific timeouts for
// specific events.
pub(crate) const DEFAULT_EVENT_TIMEOUT_SEC: u64 = 10;

// Config keys
pub(crate) const SSH_PRIV: &str = "ssh.priv";
pub(crate) const SSH_PORT: &str = "ssh.port";
pub(crate) const OVERNET_MAX_RETRY_COUNT: &str = "overnet.max_retry_count";
pub(crate) const EVENTS_TIMEOUT_SECONDS: &str = "events.timeout_secs";
#[cfg(not(test))]
pub(crate) const OVERNET_SOCKET: &str = "overnet.socket";

#[cfg(not(test))]
pub(crate) async fn get_socket() -> String {
    ffx_config::get(OVERNET_SOCKET).await.unwrap_or(DEFAULT_SOCKET.to_string())
}

#[cfg(test)]
pub(crate) async fn get_socket() -> String {
    DEFAULT_SOCKET.to_string()
}
