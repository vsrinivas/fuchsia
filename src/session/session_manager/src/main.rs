// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fuchsia_async as fasync, session_manager_lib::startup};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    startup::launch_session().await
}
