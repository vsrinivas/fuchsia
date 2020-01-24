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
use std::collections::{HashMap, HashSet};
use std::sync::Arc;

/// An implementation of the SoundPlayer for tests.
pub struct SoundPlayerService {
    // Represents the sounds that were played. Stores the id to AudioRenderUsage it was played on.
    sound_mappings: Arc<RwLock<HashMap<u32, AudioRenderUsage>>>,

    // Represents the sounds that have been added to the player. Stores the id of the added sound.
    sounds: Arc<RwLock<HashSet<u32>>>,
}

impl SoundPlayerService {
    pub fn new() -> Self {
        Self {
            sound_mappings: Arc::new(RwLock::new(HashMap::new())),
            sounds: Arc::new(RwLock::new(HashSet::new())),
        }
    }

    // Retrieve a mapping from sound id to AudioRenderUsage it was played on.
    pub fn get_mapping(&self, id: u32) -> Option<AudioRenderUsage> {
        match self.sound_mappings.read().get(&id) {
            None => None,
            Some(&val) => Some(val),
        }
    }

    // Check whether the sound with the given id was added to the Player.
    pub fn id_exists(&self, id: u32) -> bool {
        self.sounds.read().get(&id).is_some()
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
        let sounds_clone = self.sounds.clone();

        fasync::spawn(async move {
            while let Some(req) = player_stream.try_next().await.unwrap() {
                match req {
                    PlayerRequest::AddSoundFromFile {
                        id,
                        file_channel: _file_channel,
                        responder,
                    } => {
                        sounds_clone.write().insert(id);
                        responder.send(&mut Ok(())).unwrap();
                    }
                    PlayerRequest::PlaySound { id, usage, responder } => {
                        sound_mappings_clone.write().insert(id, usage);
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    }
}
