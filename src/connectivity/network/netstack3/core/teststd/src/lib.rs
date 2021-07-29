// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate provides access to the `std` crate, and is used by the Netstack3
//! Core when compiling unit tests. It provides access only to a subset of `std`
//! features which do not introduce nondeterminism - and thus cannot introduce
//! test flakes.

pub use ::std::println;
