// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::config::Config, anyhow::Error};

#[async_trait::async_trait]
pub trait Configurator {
    /// A new configator.
    fn new(config: Config) -> Result<Self, Error>
    where
        Self: Sized;

    /// Process a new codec interface.
    async fn process_new_codec(
        &mut self,
        mut device: crate::codec::CodecInterface,
    ) -> Result<(), Error>;

    /// Process a new DAI interface.
    async fn process_new_dai(&mut self, mut device: crate::dai::DaiInterface) -> Result<(), Error>;
}
