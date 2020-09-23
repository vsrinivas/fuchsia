// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::earcons::agent::CommonEarconsParams;
use crate::agent::earcons::sound_ids::{
    BLUETOOTH_CONNECTED_SOUND_ID, BLUETOOTH_DISCONNECTED_SOUND_ID,
};
use crate::agent::earcons::utils::connect_to_sound_player;
use crate::agent::earcons::utils::play_sound;
use crate::internal::event::Publisher;
use anyhow::Context;
use fidl_fuchsia_bluetooth_sys::{AccessMarker, AccessProxy, Peer, TechnologyType};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use std::collections::HashSet;
use std::iter::FromIterator;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

/// The file path for the earcon to be played for bluetooth connecting.
const BLUETOOTH_CONNECTED_FILE_PATH: &str = "bluetooth-connected.wav";

/// The file path for the earcon to be played for bluetooth disconnecting.
const BLUETOOTH_DISCONNECTED_FILE_PATH: &str = "bluetooth-disconnected.wav";

/// The `BluetoothHandler` takes care of the earcons functionality on bluetooth connection
/// and disconnection.
#[derive(Debug)]
struct BluetoothHandler {
    // Parameters common to all earcons handlers.
    common_earcons_params: CommonEarconsParams,
    connected_peers: Vec<Peer>,
}

/// Differentiates type of bluetooth earcons sound.
enum BluetoothSoundType {
    CONNECTED,
    DISCONNECTED,
}

/// Play a bluetooth earcons sound.
async fn play_bluetooth_sound(
    publisher: Publisher,
    common_earcons_params: CommonEarconsParams,
    sound_type: BluetoothSoundType,
) {
    // Connect to the SoundPlayer if not already connected.
    connect_to_sound_player(
        publisher,
        common_earcons_params.service_context.clone(),
        common_earcons_params.sound_player_connection.clone(),
    )
    .await;

    let sound_player_connection = common_earcons_params.sound_player_connection.clone();
    let sound_player_connection_lock = sound_player_connection.lock().await;
    let sound_player_added_files = common_earcons_params.sound_player_added_files.clone();

    if let Some(sound_player_proxy) = sound_player_connection_lock.as_ref() {
        match sound_type {
            BluetoothSoundType::CONNECTED => play_sound(
                &sound_player_proxy,
                BLUETOOTH_CONNECTED_FILE_PATH,
                BLUETOOTH_CONNECTED_SOUND_ID,
                sound_player_added_files.clone(),
            )
            .await
            .ok(),
            BluetoothSoundType::DISCONNECTED => play_sound(
                &sound_player_proxy,
                BLUETOOTH_DISCONNECTED_FILE_PATH,
                BLUETOOTH_DISCONNECTED_SOUND_ID,
                sound_player_added_files.clone(),
            )
            .await
            .ok(),
        };
    } else {
        fx_log_err!("[bluetooth_earcons_handler] failed to play bluetooth earcon sound: no sound player connection");
    }
}

/// Watch for peer changes on bluetoogh connection/disconnection.
pub fn watch_bluetooth_connections(
    publisher: Publisher,
    common_earcons_params: CommonEarconsParams,
    connection_active: Arc<AtomicBool>,
) {
    let handler = Arc::new(Mutex::new(BluetoothHandler {
        common_earcons_params: common_earcons_params.clone(),
        connected_peers: Vec::new(),
    }));
    fasync::Task::spawn(async move {
        let access_proxy = match common_earcons_params
            .service_context
            .lock()
            .await
            .connect_with_publisher::<AccessMarker>(publisher.clone())
            .await
            .context("[bluetooth_earcons_handler] Connecting to fuchsia.bluetooth.sys.Access")
        {
            Ok(proxy) => proxy,
            Err(e) => {
                fx_log_err!("[bluetooth_earcons_handler] Failed to connect to fuchsia.bluetooth.sys.Access: {}", e);
                return;
            }
        };
        while connection_active.load(Ordering::SeqCst) {
            match access_proxy.call_async(AccessProxy::watch_peers).await {
                Ok((updated, removed)) => {
                    let mut bluetooth_handler = handler.lock().await;
                    if updated.len() > 0 {
                        // TODO(fxbug.dev/50246): Add logging for updating bluetooth connections.
                        // Figure out which peers are connected.
                        let new_connected_peers: Vec<Peer> = updated
                            .into_iter()
                            .filter(|peer| peer.connected.unwrap_or(false))
                            .collect();

                        let bt_type_filter = |peer: &&Peer| {
                            peer.technology == Some(TechnologyType::Classic)
                                || peer.technology == Some(TechnologyType::DualMode)
                        };

                        // Create sets of the old and new connected peers.
                        let new_connected_peer_ids: HashSet<u64> = HashSet::from_iter(
                            new_connected_peers
                                .iter()
                                .filter(bt_type_filter)
                                .map(|x| x.id.unwrap().value),
                        );
                        let old_connected_peer_ids: HashSet<u64> = HashSet::from_iter(
                            bluetooth_handler
                                .connected_peers
                                .iter()
                                .filter(bt_type_filter)
                                .map(|x| x.id.unwrap().value),
                        );

                        // Figure out which ids are newly added or newly removed.
                        let added_peer_ids = new_connected_peer_ids
                            .difference(&old_connected_peer_ids)
                            .collect::<Vec<&u64>>();
                        let removed_peer_ids = old_connected_peer_ids
                            .difference(&new_connected_peer_ids)
                            .collect::<Vec<&u64>>();

                        // Update the set of connected peers.
                        bluetooth_handler.connected_peers = new_connected_peers;

                        // Play the earcon sound.
                        if added_peer_ids.len() > 0 {
                            // TODO(fxbug.dev/50246): Add logging for connecting bluetooth peer.
                            let common_earcons_params_clone =
                                bluetooth_handler.common_earcons_params.clone();
                            let publisher = publisher.clone();
                            fasync::Task::spawn(async move {
                                play_bluetooth_sound(
                                    publisher,
                                    common_earcons_params_clone,
                                    BluetoothSoundType::CONNECTED,
                                )
                                .await;
                            }).detach();
                        }
                        if removed_peer_ids.len() > 0 || removed.len() > 0 {
                            // TODO(fxbug.dev/50246): Add logging for disconnecting bluetooth peer.
                            let common_earcons_params_clone =
                                bluetooth_handler.common_earcons_params.clone();
                            let publisher = publisher.clone();
                            fasync::Task::spawn(async move {
                                play_bluetooth_sound(
                                    publisher,
                                    common_earcons_params_clone,
                                    BluetoothSoundType::DISCONNECTED,
                                )
                                .await;
                            }).detach();
                        }
                    }
                }
                Err(e) => {
                    fx_log_err!(
                        "[bluetooth_earcons_handler] Failed on call to watch bluetooth peers: {}",
                        e
                    );
                    return;
                }
            }
        }
    }).detach();
}
