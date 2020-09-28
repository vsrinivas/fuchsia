// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_async as fasync, system_health_check::set_active_configuration_healthy};

#[fasync::run(1)]
async fn main() {
    set_active_configuration_healthy().await
}
