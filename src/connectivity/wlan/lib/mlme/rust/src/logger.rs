// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_syslog as syslog, std::sync::Once};

static LOGGER_ONCE: Once = Once::new();

pub fn init() {
    // The Fuchsia syslog must not be initialized more than once per process. In the case of MLME,
    // that means we can only call it once for both the client and ap modules. Ensure this by using
    // a shared `Once::call_once()`.
    LOGGER_ONCE.call_once(|| {
        // Initialize logging with a tag that can be used to filter for forwarding to console
        syslog::init_with_tags(&["wlan"]).expect("Syslog init should not fail");
    });
}
