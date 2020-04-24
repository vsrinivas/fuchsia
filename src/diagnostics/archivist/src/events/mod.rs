// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::types::{ComponentEventStream, CHANNEL_CAPACITY},
    anyhow::Error,
    fidl_fuchsia_sys2::EventSourceProxy,
    fidl_fuchsia_sys_internal::ComponentEventProviderProxy,
    fuchsia_inspect as inspect,
    futures::{channel::mpsc, StreamExt},
};

mod core;
mod legacy;
pub(crate) mod types;

/// Subscribe to component lifecycle events.
/// |node| is the node where stats about events seen will be recorded.
pub async fn listen(
    legacy_provider: Option<ComponentEventProviderProxy>,
    event_source: Option<EventSourceProxy>,
    node: inspect::Node,
) -> Result<ComponentEventStream, Error> {
    let (sender, receiver) = mpsc::channel(CHANNEL_CAPACITY);
    if let Some(legacy_provider) = legacy_provider {
        legacy::listen(
            legacy_provider,
            sender.clone(),
            // TODO(50226): move inspect out of legacy and into the joint stream.
            node,
        )
        .unwrap_or_else(|_| {
            // TODO(50226): record this in inspect as well when moving inspect out of legacy above.
        });
    }

    if let Some(event_source) = event_source {
        core::listen(event_source, sender.clone()).await.unwrap_or_else(|_| {
            // TODO(50226): record this in inspect as well when moving inspect out of legacy above.
        });
    }

    Ok(receiver.boxed())
}
