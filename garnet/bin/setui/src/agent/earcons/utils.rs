// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::call_async,
    crate::internal::event::Publisher,
    crate::service_context::{ExternalServiceProxy, ServiceContextHandle},
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_media_sounds::{PlayerMarker, PlayerProxy},
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_debug, fx_log_err},
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::collections::HashSet,
    std::fs::File,
    std::sync::Arc,
};

/// Creates a file-based sound from a resource file.
fn resource_file(
    name: &str,
) -> Result<fidl::endpoints::ClientEnd<fidl_fuchsia_io::FileMarker>, Error> {
    // We try two paths here, because normal components see their config package data resources in
    // /pkg/data and shell tools see them in /pkgfs/packages/config-data/0/data/<pkg>.
    Ok(fidl::endpoints::ClientEnd::<fidl_fuchsia_io::FileMarker>::new(zx::Channel::from(
        fdio::transfer_fd(
            File::open(format!("/config/data/{}", name))
                .or_else(|_| {
                    File::open(format!("/pkgfs/packages/config-data/0/data/setui_service/{}", name))
                })
                .context("Opening package data file")?,
        )?,
    )))
}

/// Establish a connection to the sound player and return the proxy representing the service.
/// Will not do anything if the sound player connection is already established.
pub async fn connect_to_sound_player(
    publisher: Publisher,
    service_context_handle: ServiceContextHandle,
    sound_player_connection: Arc<Mutex<Option<ExternalServiceProxy<PlayerProxy>>>>,
) {
    let mut sound_player_connection_lock = sound_player_connection.lock().await;
    if sound_player_connection_lock.is_none() {
        *sound_player_connection_lock = service_context_handle
            .lock()
            .await
            .connect_with_publisher::<PlayerMarker>(publisher)
            .await
            .context("Connecting to fuchsia.media.sounds.Player")
            .map_err(|e| fx_log_err!("Failed to connect to fuchsia.media.sounds.Player: {}", e))
            .ok()
    }
}

/// Plays a sound with the given [id] and [file_name] via the [sound_player_proxy].
///
/// The id and file_name are expected to be unique and mapped 1:1 to each other. This allows
/// the sound file to be reused without having to load it again.
pub async fn play_sound<'a>(
    sound_player_proxy: &ExternalServiceProxy<PlayerProxy>,
    file_name: &'a str,
    id: u32,
    added_files: Arc<Mutex<HashSet<&'a str>>>,
) -> Result<(), Error> {
    // New sound, add it to the sound player set.
    if added_files.lock().await.insert(file_name) {
        let sound_file_channel = match resource_file(file_name) {
            Ok(file) => Some(file),
            Err(e) => return Err(format_err!("[earcons] Failed to convert sound file: {}", e)),
        };
        if let Some(file_channel) = sound_file_channel {
            match call_async!(sound_player_proxy => add_sound_from_file(id, file_channel)).await {
                Ok(_) => fx_log_debug!("[earcons] Added sound to Player: {}", file_name),
                Err(e) => {
                    return Err(format_err!("[earcons] Unable to add sound to Player: {}", e));
                }
            };
        }
    }

    let sound_player_proxy = sound_player_proxy.clone();
    // This fasync thread is needed so that the earcons sounds can play rapidly and not wait
    // for the previous sound to finish to send another request.
    fasync::Task::spawn(async move {
        match call_async!(sound_player_proxy => play_sound(id, AudioRenderUsage::Background)).await
        {
            Ok(_) => {
                // TODO(fxbug.dev/50246): Add inspect logging.
            }
            Err(e) => fx_log_err!("[earcons] Unable to Play sound from Player: {}", e),
        };
    })
    .detach();
    Ok(())
}
