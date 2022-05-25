// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[async_trait::async_trait]
pub trait Configurator {
    /// A new configurator.
    fn new() -> Self
    where
        Self: Sized;

    /// Process a new codec interface.
    async fn process_new_codec(&mut self, mut device: crate::codec::CodecInterface);

    /// Process a new DAI interface.
    async fn process_new_dai(&mut self, mut device: crate::dai::DaiInterface);
}
