// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_media_sounds::{PlayerRequest, PlayerRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::channel::mpsc::{Receiver, Sender};
use futures::lock::Mutex;
use futures::StreamExt;
use futures::TryStreamExt;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::sync::Arc;

const DURATION: i64 = 1000000000;

/// Send tuple of sound id and usage, refer to src/settings/service/src/agent/earcons/sound_ids.rs
/// for sound ids.
pub type SoundEventSender = Sender<(u32, AudioRenderUsage)>;

/// Receives tuple sound id and usage for earcon played. Id refers to
/// src/settings/service/src/agent/earcons/sound_ids.rs.
pub type SoundEventReceiver = Receiver<(u32, AudioRenderUsage)>;

pub async fn sound_player_service_mock(
    handles: LocalComponentHandles,
    play_counts: Arc<Mutex<HashMap<u32, u32>>>,
    sound_played_listeners: Arc<Mutex<Vec<SoundEventSender>>>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(move |mut stream: PlayerRequestStream| {
            let play_counts = Arc::clone(&play_counts);
            let sound_played_listeners = Arc::clone(&sound_played_listeners);
            fasync::Task::spawn(async move {
                while let Ok(Some(req)) = stream.try_next().await {
                    match req {
                        PlayerRequest::AddSoundFromFile { id, file: _file, responder } => {
                            let _ = play_counts.lock().await.insert(id, 0);
                            responder.send(&mut Ok(DURATION)).unwrap();
                        }
                        PlayerRequest::PlaySound { id, usage, responder } => {
                            if let Entry::Occupied(mut count) = play_counts.lock().await.entry(id) {
                                *count.get_mut() += 1;
                            }
                            for listener in sound_played_listeners.lock().await.iter_mut() {
                                // Panic if send failed, otherwise sound is played but cannot be
                                // notified.
                                listener.try_send((id, usage)).expect(
                                "SoundPlayerService::process_stream, listener failed to send id and\
                                 usage",
                            );
                            }
                            responder.send(&mut Ok(())).unwrap();
                        }
                        _ => {}
                    }
                }
            })
            .detach();
        });

    let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir).unwrap();
    fs.collect::<()>().await;

    Ok(())
}
