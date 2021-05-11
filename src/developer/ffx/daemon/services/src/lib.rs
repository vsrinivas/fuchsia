// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use services::prelude::*;

pub fn create_service_register_map() -> NameToStreamHandlerMap {
    ffx_daemon_services_macros::generate_service_map!()
}
