// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) use self::device_fidl_handler::fidl_io;

pub mod device_controller;
pub mod types;

mod device_fidl_handler;
