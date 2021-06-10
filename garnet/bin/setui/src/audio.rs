// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
pub(crate) use self::audio_default_settings::create_default_audio_stream;
pub use self::audio_default_settings::ModifiedCounters;
pub(crate) use self::audio_default_settings::{
    create_default_modified_counters, default_audio_info,
};
pub(crate) use self::audio_fidl_handler::fidl_io;
pub use self::stream_volume_control::StreamVolumeControl;
pub mod audio_controller;
pub mod policy;
pub mod types;

mod audio_default_settings;
mod audio_fidl_handler;
mod stream_volume_control;

/// Mod containing utility functions for audio-related functionality.
pub(crate) mod utils;
