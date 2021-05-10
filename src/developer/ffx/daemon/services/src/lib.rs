// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_daemon_service_echo::Echo;
use fidl::endpoints::DiscoverableService;
use services::prelude::*;

pub fn create_service_register_map() -> NameToStreamHandlerMap {
    let mut map = NameToStreamHandlerMap::new();
    // XXX(awdavies): Derive this from a macro (this has already been implemented
    // in another change via ffx_services.gni).
    map.insert(
        <<Echo as FidlService>::Service as DiscoverableService>::SERVICE_NAME.to_owned(),
        Box::new(FidlStreamHandler::<Echo>::default()),
    );
    map
}
