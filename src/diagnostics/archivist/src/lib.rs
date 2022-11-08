// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

pub mod accessor;
pub mod archivist;
mod component_lifecycle;
mod configs;
pub mod constants;
pub mod container;
pub mod diagnostics;
pub mod error;
pub mod events;
pub mod formatter;
pub mod identity;
pub mod inspect;
pub mod logs;
pub(crate) mod moniker_rewriter;
pub mod pipeline;
mod trie;
pub mod utils;

#[cfg(test)]
mod testing;

pub type ImmutableString = Box<str>;
