// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use protocols::prelude::*;

pub fn create_protocol_register_map() -> NameToStreamHandlerMap {
    ffx_daemon_protocols_macros::generate_protocol_map!()
}
