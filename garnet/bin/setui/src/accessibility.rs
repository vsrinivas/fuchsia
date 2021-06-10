// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) use self::accessibility_fidl_handler::fidl_io;

pub mod accessibility_controller;

/// Exposes the supported data types for this setting.
pub mod types;

mod accessibility_fidl_handler;
