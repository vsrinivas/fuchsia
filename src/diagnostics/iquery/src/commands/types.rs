// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::types::Error, async_trait::async_trait, serde::Serialize};

#[async_trait]
pub trait Command {
    type Result: Serialize;
    async fn execute(&self) -> Result<Self::Result, Error>;
}
