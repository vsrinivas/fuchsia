// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_helpers::component_lifecycle::ComponentLifecycleServer;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::{try_join, FutureExt, StreamExt};
use std::sync::Arc;

mod avrcp_handler;
mod battery_client;
mod media;
mod types;

#[cfg(test)]
mod tests;

use crate::avrcp_handler::process_avrcp_requests;
use crate::battery_client::process_battery_client_requests;
use crate::media::media_sessions::MediaSessions;

#[fuchsia::component(logging_tags = ["avrcp-tg"])]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let lifecycle = ComponentLifecycleServer::spawn();
    let _ = fs.dir("svc").add_fidl_service(lifecycle.fidl_service());
    let _ = fs.take_and_serve_directory_handle().expect("Unable to serve lifecycle requests");
    fasync::Task::spawn(fs.collect::<()>()).detach();

    // Shared state between AVRCP and MediaSession.
    // The current view of the media world.
    let media_state: Arc<MediaSessions> = Arc::new(MediaSessions::create());

    let watch_media_sessions_fut = media_state.watch();
    let avrcp_requests_fut = process_avrcp_requests(media_state.clone(), lifecycle);
    // Power integration is optional - the AVRCP-TG component will continue even if power
    // integration is unavailable.
    let battery_client_fut = process_battery_client_requests(media_state.clone()).map(|_| Ok(()));

    let result =
        try_join!(watch_media_sessions_fut, avrcp_requests_fut, battery_client_fut).map(|_| ());
    tracing::info!("AVRCP-TG finished with result: {:?}", result);
    result
}
