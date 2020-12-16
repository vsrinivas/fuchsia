// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # fuchsia-inspect-contrib
//!
//! This library is intended for contributions to the inspect library from clients. These are
//! patterns that clients identify in their usage of inspect that they can generalize and share.
//! It’s intended to be at a higher level than [`fuchsia-inspect`][fuchsia_inspect]. Some of the APIs
//! in this library might be promoted to the core library sometime in the future.
//!
//! [fuchsia_inspect]: crate.fuchsia_inspect.html

pub mod inspectable;
#[macro_use]
pub mod log;
pub mod nodes;
