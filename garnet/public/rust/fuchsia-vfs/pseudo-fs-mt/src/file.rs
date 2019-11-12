// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module holding different kinds of pseudo files and their building blocks.

/// File nodes with per-connection buffers.
pub mod pcb;

/// File nodes backed by VMOs.
pub mod vmo;

pub mod test_utils;

mod common;
