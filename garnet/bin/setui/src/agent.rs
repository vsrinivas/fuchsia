// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This mod defines Authority, a trait whose interface allows components to
/// to participate in different lifespans.
pub mod base;

/// This mod provides a concrete implementation of the agent authority.
pub mod authority_impl;

/// Agent for playing earcons sounds.
pub mod earcons_agent;

/// Agent for rehydrating actions for restore
pub mod restore_agent;

/// Earcons handlers.
pub mod volume_change_earcons_handler;
