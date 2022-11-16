// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

mod accessor;
pub mod archivist;
mod component_lifecycle;
mod configs;
pub mod constants;
mod container;
mod diagnostics;
mod error;
pub mod events;
pub mod formatter;
mod identity;
mod inspect;
pub mod logs;
mod moniker_rewriter;
mod pipeline;
mod trie;
mod utils;

#[cfg(test)]
mod testing;

pub type ImmutableString = Box<str>;
