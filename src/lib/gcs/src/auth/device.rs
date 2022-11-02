// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, structured_ui};

/// Performs steps to get a refresh token from scratch.
///
/// This may involve user interaction such as opening a browser window..
pub async fn new_refresh_token<I>(_ui: &I) -> Result<String>
where
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("device_new_refresh_token");
    unimplemented!();
}
