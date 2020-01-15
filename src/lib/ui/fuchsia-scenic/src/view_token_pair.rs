// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_ui_views::{ViewHolderToken, ViewToken};
use fuchsia_zircon::EventPair;

pub struct ViewTokenPair {
    pub view_token: ViewToken,
    pub view_holder_token: ViewHolderToken,
}

impl ViewTokenPair {
    pub fn new() -> Result<ViewTokenPair, Error> {
        let (raw_view_token, raw_view_holder_token) = EventPair::create()?;
        let token_pair = ViewTokenPair {
            view_token: ViewToken { value: raw_view_token },
            view_holder_token: ViewHolderToken { value: raw_view_holder_token },
        };

        Ok(token_pair)
    }
}
