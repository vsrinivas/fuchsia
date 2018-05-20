// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust bindings to common Fuchsia device libraries
//! Currently pty only implemented, others to follow

#![deny(warnings)]
//#![deny(missing_docs)]

#![allow(dead_code)]
extern crate fuchsia_zircon as zx;
#[macro_use]
extern crate fdio;
#[macro_use]
extern crate failure;

pub mod pty;
