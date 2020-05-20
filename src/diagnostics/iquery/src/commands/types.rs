// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::types::Error, async_trait::async_trait};

#[async_trait]
pub trait Command {
    async fn execute(&self) -> Result<(), Error>;
}
