// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_helpers::component_lifecycle::ComponentLifecycleServer,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{try_join, StreamExt},
    std::sync::Arc,
};

mod avrcp_handler;
mod media;
mod types;

#[cfg(test)]
mod tests;

use crate::avrcp_handler::process_avrcp_requests;
use crate::media::media_sessions::MediaSessions;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["avrcp-tg"]).expect("Unable to initialize logger");
    fuchsia_syslog::set_verbosity(1);

    let mut fs = ServiceFs::new();
    let lifecycle = ComponentLifecycleServer::spawn();
    fs.dir("svc").add_fidl_service(lifecycle.fidl_service());
    fs.take_and_serve_directory_handle().expect("Unable to serve lifecycle requests");
    fasync::spawn(fs.collect::<()>());

    // Shared state between AVRCP and MediaSession.
    // The current view of the media world.
    let media_state: Arc<MediaSessions> = Arc::new(MediaSessions::create());

    let watch_media_sessions_fut = media_state.watch();
    let avrcp_requests_fut = process_avrcp_requests(media_state.clone(), lifecycle);

    try_join!(watch_media_sessions_fut, avrcp_requests_fut).map(|_| ())
}
