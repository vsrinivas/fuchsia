// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

mod sound_player;
#[cfg(test)]
mod test;

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

type Result<T> = std::result::Result<T, Error>;

fn spawn_log_error(fut: impl Future<Output = Result<()>> + 'static) {
    fasync::spawn_local(fut.unwrap_or_else(|e| fuchsia_syslog::fx_log_err!("{}", e)))
}

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["soundplayer"]).expect("Initializing syslogger");
    fuchsia_syslog::fx_log_info!("Initializing Fuchsia SoundPlayer Service");

    let mut server = ServiceFs::new_local();
    server.dir("svc").add_fidl_service(
        |request_stream: fidl_fuchsia_media_sounds::PlayerRequestStream| {
            // Each connecting client gets its own instance of SoundPlayer.
            let sound_player = self::sound_player::SoundPlayer::new();
            spawn_log_error(sound_player.serve(request_stream));
        },
    );
    server.take_and_serve_directory_handle().expect("To serve fuchsia.sounds.Player services");

    server.collect::<()>().await;
}
