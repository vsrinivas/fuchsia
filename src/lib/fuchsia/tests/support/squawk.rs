// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn log_messages_inner() {
    tracing::trace!("Hello, World!");
    tracing::debug!("Hello, World!");
    tracing::info!("Hello, World!");
    tracing::warn!("Hello, World!");
    tracing::error!("Hello, World!");
}

pub async fn log_expected_messages_async() {
    log_messages_inner();

    #[cfg(target_os = "fuchsia")]
    {
        use fidl_fuchsia_fuchsialibtest::ConnectOnDoneMarker;
        use fuchsia_component::client::connect_to_protocol;
        use futures::StreamExt;

        // tell the test parent we're done, wait for it to send an event back
        let on_done = connect_to_protocol::<ConnectOnDoneMarker>().unwrap();
        let mut events = on_done.take_event_stream();
        let _ = events.next().await.unwrap();
    }
}

pub fn log_expected_messages() {
    log_messages_inner();

    #[cfg(target_os = "fuchsia")]
    {
        use fidl_fuchsia_fuchsialibtest::{ConnectOnDoneMarker, ConnectOnDoneSynchronousProxy};
        use fuchsia_component::client::connect_channel_to_protocol;
        use fuchsia_zircon::{Channel, Time};

        let (client_end, server_end) = Channel::create().unwrap();
        connect_channel_to_protocol::<ConnectOnDoneMarker>(server_end).unwrap();
        let on_done = ConnectOnDoneSynchronousProxy::new(client_end);
        on_done.wait_for_event(Time::INFINITE).unwrap();
    }
}
