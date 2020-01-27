// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_media_sounds::{PlayerMarker, PlayerRequest};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

/// An implementation of the SoundPlayer for tests.
pub struct SoundPlayerService {
    // Represents the number of times the sound has been played in total.
    play_counts: Arc<RwLock<HashMap<u32, u32>>>,

    // Represents the sounds that were played. Stores the id to AudioRenderUsage it was played on.
    sound_mappings: Arc<RwLock<HashMap<u32, AudioRenderUsage>>>,
}

impl SoundPlayerService {
    pub fn new() -> Self {
        Self {
            play_counts: Arc::new(RwLock::new(HashMap::new())),
            sound_mappings: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    // Retrieve a mapping from sound id to AudioRenderUsage it was played on.
    pub fn get_usage_by_id(&self, id: u32) -> Option<AudioRenderUsage> {
        match self.sound_mappings.read().get(&id) {
            None => None,
            Some(&val) => Some(val),
        }
    }

    // Check whether the sound with the given id was added to the Player.
    pub fn id_exists(&self, id: u32) -> bool {
        self.play_counts.read().get(&id).is_some()
    }

    // Get the number of times the sound with the given id has played.
    pub fn get_play_count(&self, id: u32) -> Option<u32> {
        match self.play_counts.read().get(&id) {
            None => None,
            Some(&val) => Some(val),
        }
    }
}

impl Service for SoundPlayerService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == PlayerMarker::NAME;
    }

    fn process_stream(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut player_stream = ServerEnd::<PlayerMarker>::new(channel).into_stream()?;

        let sound_mappings_clone = self.sound_mappings.clone();
        let play_counts_clone = self.play_counts.clone();

        fasync::spawn(async move {
            while let Some(req) = player_stream.try_next().await.unwrap() {
                match req {
                    PlayerRequest::AddSoundFromFile {
                        id,
                        file_channel: _file_channel,
                        responder,
                    } => {
                        play_counts_clone.write().insert(id, 0);
                        responder.send(&mut Ok(())).unwrap();
                    }
                    PlayerRequest::PlaySound { id, usage, responder } => {
                        sound_mappings_clone.write().insert(id, usage);
                        play_counts_clone.write().entry(id).and_modify(|count| *count += 1);
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    }
}
