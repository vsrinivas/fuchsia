// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::tests::fakes::base::Service;
use failure::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_media::AudioRenderUsage;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

/// An implementation of audio core service that captures the set gains on
/// usages.
pub struct AudioCoreService {
    audio_streams: Arc<RwLock<HashMap<AudioRenderUsage, f32>>>,
}

impl AudioCoreService {
    pub fn new() -> Self {
        Self { audio_streams: Arc::new(RwLock::new(HashMap::new())) }
    }

    pub fn get_gain(&self, usage: AudioRenderUsage) -> Option<f32> {
        if let Some(gain) = self.audio_streams.read().get(&usage) {
            let gain_copy = *gain;
            return Some(gain_copy);
        }
        return None;
    }
}

impl Service for AudioCoreService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == fidl_fuchsia_media::AudioCoreMarker::NAME;
    }

    fn process_stream(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_media::AudioCoreMarker>::new(channel).into_stream()?;

        let streams_clone = self.audio_streams.clone();
        fasync::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_media::AudioCoreRequest::SetRenderUsageGain {
                        usage,
                        gain_db,
                        control_handle: _,
                    } => {
                        (*streams_clone.write()).insert(usage, gain_db);
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    }
}
