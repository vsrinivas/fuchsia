// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]

use protocols::prelude::*;

pub fn create_protocol_register_map() -> NameToStreamHandlerMap {
    ffx_daemon_protocols_macros::generate_protocol_map!()
}
