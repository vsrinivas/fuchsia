// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use self::audio_controller::create_default_audio_stream;
pub use self::audio_controller::spawn_audio_controller;
pub use self::audio_fidl_handler::spawn_audio_fidl_handler;

mod audio_controller;
mod audio_fidl_handler;
