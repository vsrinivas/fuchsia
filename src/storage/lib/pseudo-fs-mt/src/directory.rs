// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module holding different kinds of pseudo directories and thier buidling blocks.

#[macro_use]
pub mod test_utils;

#[macro_use]
mod common;
mod connection;
mod traversal_position;
mod watchers;

pub mod immutable;
pub mod mutable;

pub mod simple;

pub mod dirents_sink;
pub mod entry;
pub mod entry_container;
pub mod read_dirents;
