// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Raw OpenThread API Bindings #
//!
//! This crate provides the raw OpenThread API bindings to Rust.
//! The entire API surface is unsafe and, in general, should not
//! be used directly. Instead, you should use a safe wrapper crate
//! such as [`::openthread_rust`].

mod bindings;
pub use bindings::*;

pub mod spinel;
