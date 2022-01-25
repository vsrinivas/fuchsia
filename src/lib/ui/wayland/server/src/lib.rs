// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod test_protocol;

pub mod alpha_compositing;
pub mod aura_shell;
pub mod buffer;
pub mod client;
pub mod compositor;
pub mod data_device_manager;
pub mod dispatcher;
pub mod display;
pub mod linux_dmabuf;
pub mod object;
pub mod output;
pub mod pointer_constraints;
pub mod registry;
pub mod relative_pointer;
pub mod scenic;
pub mod seat;
pub mod secure_output;
pub mod shm;
pub mod subcompositor;
pub mod viewporter;
pub mod xdg_shell;
