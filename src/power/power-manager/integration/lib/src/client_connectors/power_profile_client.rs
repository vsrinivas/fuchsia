// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv,
    fidl_fuchsia_power_profile as fprofile, fuchsia_async as fasync,
    futures::{channel::mpsc, StreamExt},
    tracing::*,
};

/// Convenience type for interacting with the Power Manager's power profile service.
pub struct PowerProfileClient {
    _watcher_task: fasync::Task<()>,
    profile_receiver: mpsc::Receiver<fprofile::Profile>,
}

impl PowerProfileClient {
    pub async fn new(test_env: &TestEnv) -> Self {
        let (mut profile_sender, profile_receiver) = mpsc::channel(1);
        let proxy = test_env.connect_to_protocol::<fprofile::WatcherMarker>();

        let _watcher_task = fasync::Task::local(async move {
            while let Ok(profile) = proxy.watch().await {
                info!("Received power profile: {:?}", profile);
                profile_sender.try_send(profile).expect("Failed to notify power profile change");
            }
        });

        Self { _watcher_task, profile_receiver }
    }

    /// Returns the next power profile that the watcher has received, or hangs until one is
    /// received.
    pub async fn get_power_profile(&mut self) -> fprofile::Profile {
        self.profile_receiver.next().await.expect("Failed to wait for power profile")
    }
}
