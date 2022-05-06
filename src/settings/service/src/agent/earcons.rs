// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Agent for playing earcons sounds.
pub(crate) mod agent;

/// Earcons utilities.
pub(crate) mod sound_ids;
pub(super) mod utils;

/// Earcons handlers.
pub(crate) mod bluetooth_handler;
mod volume_change_handler;
