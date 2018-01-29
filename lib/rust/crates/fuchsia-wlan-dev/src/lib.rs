// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
#[macro_use]
extern crate fdio;
extern crate fidl;
extern crate fuchsia_zircon as zircon;
extern crate garnet_lib_wlan_fidl as wlan;

// TODO: write non-sys functions
pub mod sys;
