// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::time::Duration;

pub(crate) const FASTBOOT_CHECK_INTERVAL: Duration = Duration::from_secs(3);
pub(crate) const MDNS_BROADCAST_INTERVAL: Duration = Duration::from_secs(20);

const GRACE_INTERVAL: Duration = Duration::from_secs(5);

pub(crate) const FASTBOOT_MAX_AGE: Duration =
    // Rust(#76416): FASTBOOT_CHECK_INTERVAL.saturating_add(GRACE_INTERVAL);
    Duration::from_secs(FASTBOOT_CHECK_INTERVAL.as_secs() + GRACE_INTERVAL.as_secs());
pub(crate) const MDNS_MAX_AGE: Duration =
    Duration::from_secs(MDNS_BROADCAST_INTERVAL.as_secs() + GRACE_INTERVAL.as_secs());
pub(crate) const ZEDBOOT_MAX_AGE: Duration = Duration::from_secs(2);

// Delay between retry attempts to find the RCS.
pub(crate) const RETRY_DELAY: Duration = Duration::from_millis(200);

// Config keys
pub(crate) const SSH_PRIV: &str = "ssh.priv";

pub const LOG_FILE_PREFIX: &str = "ffx.daemon";

#[cfg(not(test))]
pub async fn get_socket() -> String {
    const OVERNET_SOCKET: &str = "overnet.socket";
    const DEFAULT_SOCKET: &str = "/tmp/ascendd";
    ffx_config::get(OVERNET_SOCKET).await.unwrap_or(DEFAULT_SOCKET.to_string())
}

#[cfg(test)]
pub async fn get_socket() -> String {
    std::thread_local! {
        static DEFAULT_SOCKET: String = {
            tempfile::Builder::new()
                .prefix("ascendd_for_test")
                .suffix(".sock")
                .tempfile().unwrap()
                .path()
                .file_name().and_then(std::ffi::OsStr::to_str).unwrap().to_string()
        };
    }

    DEFAULT_SOCKET.with(|k| k.clone())
}

pub(crate) const CURRENT_EXE_HASH: &str = "current.hash";
