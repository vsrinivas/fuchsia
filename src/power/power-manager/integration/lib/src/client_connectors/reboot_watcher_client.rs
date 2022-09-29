// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv,
    fidl_fuchsia_hardware_power_statecontrol as fpower, fuchsia_async as fasync,
    futures::{channel::mpsc, StreamExt, TryStreamExt},
    tracing::*,
};

/// Convenience type for interacting with the Power Manager's RebootWatcher service.
pub struct RebootWatcherClient {
    _watcher_task: fasync::Task<()>,
    reboot_reason_receiver: mpsc::Receiver<fpower::RebootReason>,
}

impl RebootWatcherClient {
    pub async fn new(test_env: &TestEnv) -> Self {
        let (mut reboot_reason_sender, reboot_reason_receiver) = mpsc::channel(1);

        // Create a new watcher proxy/stream and register the proxy end with Power Manager
        let watcher_register_proxy =
            test_env.connect_to_protocol::<fpower::RebootMethodsWatcherRegisterMarker>();
        let (watcher_client, mut watcher_request_stream) =
            fidl::endpoints::create_request_stream::<fpower::RebootMethodsWatcherMarker>().unwrap();
        watcher_register_proxy
            .register_with_ack(watcher_client)
            .await
            .expect("Failed to register reboot watcher");

        let _watcher_task = fasync::Task::local(async move {
            while let Some(fpower::RebootMethodsWatcherRequest::OnReboot { reason, responder }) =
                watcher_request_stream.try_next().await.unwrap()
            {
                info!("Received reboot reason: {:?}", reason);
                let _ = responder.send();
                reboot_reason_sender.try_send(reason).expect("Failed to notify reboot reason");
            }
        });

        Self { _watcher_task, reboot_reason_receiver }
    }

    /// Returns the next reboot reason that the reboot watcher has received, or hangs until one is
    /// received.
    pub async fn get_reboot_reason(&mut self) -> fpower::RebootReason {
        self.reboot_reason_receiver.next().await.expect("Failed to wait for reboot reason")
    }
}
