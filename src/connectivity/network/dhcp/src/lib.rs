// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![deny(warnings)]

pub mod configuration;
pub mod protocol;
pub mod server;
pub mod stash;

//TODO(atait): Add tests exercising the public API of this library.
